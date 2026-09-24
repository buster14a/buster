/* Standalone miniature producer fixture for #1020. The integration owner
 * registers this file in the shared service/hosted test graph after #1018's
 * importer and the production pre-timing boundary are wired.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../../src/buster/lib/hash.c"
#include "retirement_correctness.c"
#include "retirement_artifact_service.c"

#define BQ_TEST_ROWS 6u
#define BQ_TEST_CHECKS (BQ_RETIREMENT_CHECK_COUNT - 1u)

static unsigned assertions, failures, launches;
#define CHECK(expression) do { \
    assertions += 1; \
    if (!(expression)) { \
        failures += 1; \
        fprintf(stderr, "RETIREMENT_CORRECTNESS_TEST line=%d: %s\n", __LINE__, #expression); \
    } \
} while (0)

typedef struct BqCorrectnessFixture
{
    BqRetirementPrepared prepared;
    BqRetirementTrustedRow trusted[BQ_TEST_ROWS];
    BqRetirementRequiredCheck required[BQ_TEST_CHECKS];
    BqRetirementCheckResult checks[BQ_TEST_CHECKS], check_facts[BQ_TEST_CHECKS];
    BqRetirementRowFact supplied[BQ_TEST_ROWS], facts[BQ_TEST_ROWS];
    uint32_t identity_workspace[BQ_TEST_ROWS * 2u + 1u];
    uint8_t census_workspace[3];
    BqRetirementCorrectness gate;
} BqCorrectnessFixture;

static void digest(char value[65], char fill)
{
    memset(value, fill, 64);
    value[64] = 0;
}

static void fixture_init(BqCorrectnessFixture* fixture)
{
    *fixture = (BqCorrectnessFixture){0};
    BqRetirementPrepared* prepared = &fixture->prepared;
    digest(prepared->preparation_sha256, 'a');
    digest(prepared->support_sha256, 'b');
    digest(prepared->census_sha256, 'c');
    prepared->rows = BQ_TEST_ROWS;
    prepared->object_rows = 3;
    prepared->native_target = 1;
    for (unsigned side = 0; side < 2; side += 1)
    {
        digest(prepared->source_sha256[side], side ? 'e' : 'd');
        digest(prepared->binary_sha256[side], side ? '2' : '1');
    }
    for (unsigned i = 0; i < BQ_TEST_CHECKS; i += 1)
    {
        BqRetirementRequiredCheck* required = &fixture->required[i];
        BqRetirementCheckResult* check = &fixture->checks[i];
        required->kind = i + 1;
        required->rows = i == 0 ? 3 : i == 2 || i == 3 ? 5 : BQ_TEST_ROWS;
        required->target = 0;
        digest(required->command_sha256, (char)('3' + i));
        digest(required->configuration_sha256, (char)('9' - i));
        check->kind = required->kind;
        check->target = required->target;
        check->rows = required->rows;
        memcpy(check->command_sha256, required->command_sha256, 65);
        memcpy(check->configuration_sha256, required->configuration_sha256, 65);
        memcpy(check->preparation_sha256, prepared->preparation_sha256, 65);
        digest(check->receipt_sha256, (char)('a' + i));
        memcpy(required->receipt_sha256, check->receipt_sha256, 65);
        for (unsigned side = 0; side < 2; side += 1)
        {
            memcpy(check->source_sha256[side], prepared->source_sha256[side], 65);
            memcpy(check->binary_sha256[side], prepared->binary_sha256[side], 65);
        }
    }
    for (unsigned i = 0; i < BQ_TEST_ROWS; i += 1)
    {
        BqRetirementTrustedRow* row = &fixture->trusted[i];
        BqRetirementRowFact* fact = &fixture->supplied[i];
        row->row = fact->row = i;
        row->census_row = fact->census_row = i == 0 || i == 1 ? 0 :
            i == 2 || i == 4 ? 1 : 2;
        row->target = i == 4 ? 2 : 1;
        row->stage = i == 1 || i == 4 ? BQ_RETIREMENT_STAGE_LINK :
                     i == 5 ? BQ_RETIREMENT_STAGE_SELF_HOST : BQ_RETIREMENT_STAGE_OBJECT;
        row->classification = i == 2 ? 2 : i == 3 ? 4 : 1;
        row->compiler_eligible = fact->compiler_eligible = i != 3;
        row->code_obligation = i != 3;
        row->execution_obligation = row->stage != BQ_RETIREMENT_STAGE_OBJECT;
        digest(row->identity_sha256, (char)('1' + i));
        digest(row->source_sha256, (char)('a' + i));
        digest(row->configuration_sha256, (char)('4' + i));
        if (i == 3) digest(row->skip_proof_sha256, 'e');
        if (i == 1 || i == 5) digest(row->independent_oracle_sha256, 'f');
        fact->runtime_eligible = i == 1 || i == 5;
        fact->code_eligible = i != 0 && i != 3;
        for (unsigned side = 0; side < 2 && i != 3; side += 1)
        {
            BqRetirementObservedSide* observed = &fact->side[side];
            digest(observed->compiler_command_sha256, side ? 'b' : 'a');
            memcpy(row->compiler_command_sha256[side], observed->compiler_command_sha256, 65);
            digest(observed->artifact_sha256, side ? 'd' : 'c');
            observed->semantic_pass = 1;
            observed->runtime_exit = -1;
            if (i == 0 && !side)
                memcpy(observed->code_sha256,
                    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 65);
            else digest(observed->code_sha256, side ? '2' : '1');
            observed->code_bytes = i == 0 && !side ? 0 : 7;
            if (fact->runtime_eligible)
            {
                digest(observed->runtime_command_sha256, side ? '8' : '7');
                memcpy(row->runtime_command_sha256[side], observed->runtime_command_sha256, 65);
                memcpy(observed->runtime_output_sha256, row->independent_oracle_sha256, 65);
                observed->runtime_exit = 0;
            }
        }
    }
}

static void begin_fixture(BqCorrectnessFixture* fixture)
{
    CHECK(bq_retirement_correctness_begin(&fixture->gate, &fixture->prepared, fixture->trusted,
        fixture->required, BQ_TEST_CHECKS, fixture->check_facts, fixture->facts,
        fixture->identity_workspace, BQ_TEST_ROWS * 2u + 1u,
        fixture->census_workspace, 3));
}

static void checks_fixture(BqCorrectnessFixture* fixture)
{
    for (unsigned i = 0; i < BQ_TEST_CHECKS; i += 1)
        CHECK(bq_retirement_correctness_check(&fixture->gate, &fixture->checks[i]));
}

static void rows_fixture(BqCorrectnessFixture* fixture)
{
    for (unsigned i = 0; i < BQ_TEST_ROWS; i += 1)
        CHECK(bq_retirement_correctness_row(&fixture->gate, &fixture->supplied[i]));
}

static void launch_if_ready(BqRetirementCorrectness const* gate)
{
    if (bq_retirement_correctness_ready(gate)) launches += 1;
}

static void test_valid(void)
{
    BqCorrectnessFixture fixture;
    fixture_init(&fixture);
    begin_fixture(&fixture);
    launch_if_ready(&fixture.gate);
    CHECK(launches == 0);
    checks_fixture(&fixture);
    rows_fixture(&fixture);
    CHECK(bq_retirement_correctness_finish(&fixture.gate));
    CHECK(fixture.gate.eligible_rows == 5 && fixture.gate.rows_done == BQ_TEST_ROWS);
    CHECK(fixture.facts[0].side[0].code_bytes == 0 && !fixture.facts[0].code_eligible &&
          fixture.facts[0].side[1].code_bytes == 7);
    CHECK(!fixture.facts[3].compiler_eligible && fixture.facts[3].row == 3 &&
          fixture.facts[4].compiler_eligible && !fixture.facts[4].runtime_eligible);
    launch_if_ready(&fixture.gate);
    CHECK(launches == 1);
    fixture.facts[1].side[0].artifact_sha256[0] = 'e';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.facts[1].side[0].artifact_sha256[0] = 'c';
    CHECK(bq_retirement_correctness_ready(&fixture.gate));
    fixture.trusted[1].independent_oracle_sha256[0] = 'e';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.trusted[1].independent_oracle_sha256[0] = 'f';
    fixture.trusted[1].runtime_command_sha256[1][0] = 'f';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.trusted[1].runtime_command_sha256[1][0] = '8';
    fixture.trusted[2].compiler_command_sha256[0][0] = 'f';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.trusted[2].compiler_command_sha256[0][0] = 'a';
    fixture.required[0].command_sha256[0] = 'f';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.required[0].command_sha256[0] = '3';
    fixture.required[0].receipt_sha256[0] = 'f';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.required[0].receipt_sha256[0] = 'a';
    fixture.check_facts[0].receipt_sha256[0] = 'f';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.check_facts[0].receipt_sha256[0] = 'a';
    fixture.trusted[2].compiler_eligible = 0;
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.trusted[2].compiler_eligible = 1;
    fixture.trusted[2].source_sha256[0] = '0';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.trusted[2].source_sha256[0] = 'c';
    fixture.gate.prepared.binary_sha256[0][0] = 'f';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
}

static void test_bad_checks(void)
{
    for (unsigned fault = 0; fault < 10; fault += 1)
    {
        BqCorrectnessFixture fixture;
        fixture_init(&fixture);
        begin_fixture(&fixture);
        BqRetirementCheckResult* check = &fixture.checks[0];
        if (fault == 0) check->failures = 1;
        if (fault == 1) check->timed_out = 1;
        if (fault == 2) check->out_of_memory = 1;
        if (fault == 3) check->exit_code = 1;
        if (fault == 4) check->rows -= 1;
        if (fault == 5) check->preparation_sha256[0] = '0';
        if (fault == 6) check->binary_sha256[1][0] = '0';
        if (fault == 7) check->command_sha256[0] = '0';
        if (fault == 8) check->receipt_sha256[0] = 0;
        if (fault == 9) check->receipt_sha256[0] = 'b';
        CHECK(!bq_retirement_correctness_check(&fixture.gate, check));
        CHECK(!bq_retirement_correctness_check(&fixture.gate, &fixture.checks[1]));
        launch_if_ready(&fixture.gate);
        CHECK(launches == 1);
    }
}

static void test_bad_rows(void)
{
    for (unsigned fault = 0; fault < 13; fault += 1)
    {
        BqCorrectnessFixture fixture;
        fixture_init(&fixture);
        begin_fixture(&fixture);
        checks_fixture(&fixture);
        BqRetirementRowFact* row = &fixture.supplied[0];
        if (fault == 0) row->row = 1;
        if (fault == 1) row->compiler_eligible = 0;
        if (fault == 2) row->side[0].fallback_count = 1;
        if (fault == 3) row->side[0].semantic_pass = 0;
        if (fault == 4) row->side[0].compiler_exit = 1;
        if (fault == 5) row->side[0].timed_out = 1;
        if (fault == 6) row->side[0].out_of_memory = 1;
        if (fault == 7) row->side[0].code_sha256[0] = '0';
        if (fault == 8) row->side[0].runtime_command_sha256[0] = 'a';
        if (fault == 9) row->side[0].runtime_exit = 0;
        if (fault == 10) row->side[0].artifact_sha256[0] = 0;
        if (fault == 11) row->code_eligible = 1;
        if (fault == 12) row->side[0].compiler_command_sha256[0] = 'f';
        CHECK(!bq_retirement_correctness_row(&fixture.gate, row));
        CHECK(!bq_retirement_correctness_finish(&fixture.gate));
        launch_if_ready(&fixture.gate);
        CHECK(launches == 1);
    }
    for (unsigned fault = 0; fault < 6; fault += 1)
    {
        BqCorrectnessFixture fixture;
        fixture_init(&fixture);
        begin_fixture(&fixture);
        checks_fixture(&fixture);
        CHECK(bq_retirement_correctness_row(&fixture.gate, &fixture.supplied[0]));
        BqRetirementRowFact* row = &fixture.supplied[1];
        if (fault == 0) row->side[0].runtime_output_sha256[0] = '0';
        if (fault == 1) row->side[0].runtime_exit = 1;
        if (fault == 2) row->runtime_eligible = 0;
        if (fault == 3) row->side[0].runtime_command_sha256[0] = 0;
        if (fault == 4) row->side[0].code_bytes = 0;
        if (fault == 5) row->side[0].runtime_command_sha256[0] = 'f';
        CHECK(!bq_retirement_correctness_row(&fixture.gate, row));
        launch_if_ready(&fixture.gate);
        CHECK(launches == 1);
    }
}

static void test_bad_import(void)
{
    for (unsigned fault = 0; fault < 15; fault += 1)
    {
        BqCorrectnessFixture fixture;
        fixture_init(&fixture);
        if (fault == 0) fixture.trusted[1].row = 0;
        if (fault == 1) fixture.trusted[3].compiler_eligible = 1;
        if (fault == 2) fixture.trusted[3].skip_proof_sha256[0] = 0;
        if (fault == 3) fixture.trusted[4].independent_oracle_sha256[0] = 'f';
        if (fault == 4) fixture.prepared.binary_sha256[1][0] = 0;
        if (fault == 5) fixture.required[5].kind = fixture.required[0].kind;
        if (fault == 6) fixture.prepared.rows = 0;
        if (fault == 7) memcpy(fixture.trusted[2].identity_sha256, fixture.trusted[0].identity_sha256, 65);
        if (fault == 8) fixture.trusted[2].census_row = 0;
        if (fault == 9) fixture.prepared.object_rows = 2;
        if (fault == 10) fixture.required[3].rows = 4;
        if (fault == 11) fixture.trusted[0].compiler_command_sha256[0][0] = 0;
        if (fault == 12) fixture.trusted[1].runtime_command_sha256[0][0] = 0;
        if (fault == 13) fixture.trusted[4].runtime_command_sha256[0][0] = 'f';
        if (fault == 14) fixture.required[0].receipt_sha256[0] = 0;
        CHECK(!bq_retirement_correctness_begin(&fixture.gate, &fixture.prepared,
            fixture.trusted, fixture.required, BQ_TEST_CHECKS,
            fixture.check_facts, fixture.facts, fixture.identity_workspace,
            BQ_TEST_ROWS * 2u + 1u, fixture.census_workspace, 3));
        launch_if_ready(&fixture.gate);
        CHECK(launches == 1);
    }
}

static void test_partial_and_sealed(void)
{
    BqCorrectnessFixture fixture;
    fixture_init(&fixture);
    begin_fixture(&fixture);
    for (unsigned i = 0; i + 1 < BQ_TEST_CHECKS; i += 1)
        CHECK(bq_retirement_correctness_check(&fixture.gate, &fixture.checks[i]));
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    CHECK(!bq_retirement_correctness_row(&fixture.gate, &fixture.supplied[0]));
    launch_if_ready(&fixture.gate);
    CHECK(launches == 1);

    fixture_init(&fixture);
    begin_fixture(&fixture);
    checks_fixture(&fixture);
    for (unsigned i = 0; i + 1 < BQ_TEST_ROWS; i += 1)
        CHECK(bq_retirement_correctness_row(&fixture.gate, &fixture.supplied[i]));
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    CHECK(!bq_retirement_correctness_finish(&fixture.gate));
    launch_if_ready(&fixture.gate);
    CHECK(launches == 1);

    fixture_init(&fixture);
    begin_fixture(&fixture);
    checks_fixture(&fixture);
    rows_fixture(&fixture);
    CHECK(bq_retirement_correctness_finish(&fixture.gate));
    CHECK(bq_retirement_correctness_ready(&fixture.gate));
    CHECK(!bq_retirement_correctness_row(&fixture.gate, &fixture.supplied[0]));
    launch_if_ready(&fixture.gate);
    CHECK(launches == 1);
}

/* This checks the bounded full-population data path. It uses generated fixture
 * identities and says nothing about the real census or semantic oracles. */
static void test_large_population(void)
{
    enum { rows = 78912 };
    BqCorrectnessFixture fixture;
    fixture_init(&fixture);
    BqRetirementTrustedRow* trusted = calloc(rows, sizeof(*trusted));
    BqRetirementRowFact* supplied = calloc(rows, sizeof(*supplied));
    BqRetirementRowFact* facts = calloc(rows, sizeof(*facts));
    uint32_t* identities = calloc(rows * 2u + 1u, sizeof(*identities));
    uint8_t* census = calloc(rows, sizeof(*census));
    bool allocated = trusted && supplied && facts && identities && census;
    CHECK(allocated);
    if (allocated)
    {
        uint32_t eligible = 0;
        for (uint32_t i = 0; i < rows; i += 1)
        {
            BqRetirementTrustedRow* row = &trusted[i];
            BqRetirementRowFact* fact = &supplied[i];
            *row = fixture.trusted[0];
            *fact = fixture.supplied[0];
            row->row = fact->row = row->census_row = fact->census_row = i;
            Sha256 hash;
            sha256_init(&hash);
            sha256_add(&hash, &i, sizeof(i));
            sha256_finish_hex(&hash, row->identity_sha256);
            if (i % 16u == 0)
            {
                row->compiler_eligible = fact->compiler_eligible = 0;
                row->classification = 4;
                digest(row->skip_proof_sha256, 'e');
                memset(row->compiler_command_sha256, 0, sizeof(row->compiler_command_sha256));
                fact->side[0] = fact->side[1] = (BqRetirementObservedSide){0};
            }
            else eligible += 1;
        }
        fixture.prepared.rows = fixture.prepared.object_rows = rows;
        for (uint32_t i = 0; i < BQ_TEST_CHECKS; i += 1)
        {
            fixture.required[i].rows = fixture.checks[i].rows =
                i == 2 || i == 3 ? eligible : rows;
        }
        CHECK(bq_retirement_correctness_begin(&fixture.gate, &fixture.prepared,
            trusted, fixture.required, BQ_TEST_CHECKS, fixture.check_facts, facts,
            identities, rows * 2u + 1u, census, rows));
        checks_fixture(&fixture);
        for (uint32_t i = 0; i < rows; i += 1)
            CHECK(bq_retirement_correctness_row(&fixture.gate, &supplied[i]));
        CHECK(bq_retirement_correctness_finish(&fixture.gate));
        CHECK(bq_retirement_correctness_ready(&fixture.gate));
        facts[rows - 1].compiler_eligible = 0;
        CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    }
    free(census);
    free(identities);
    free(facts);
    free(supplied);
    free(trusted);
}

static bool copy_frozen_artifact(int directory, char const* source, char const* name)
{
    int input = open(source, O_RDONLY | O_CLOEXEC);
    int output = input >= 0 ? openat(directory, name, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600) : -1;
    bool ok = input >= 0 && output >= 0;
    char buffer[16384];
    while (ok)
    {
        ssize_t size = read(input, buffer, sizeof(buffer));
        if (size < 0 && errno == EINTR) continue;
        if (size <= 0)
        {
            ok = size == 0;
            break;
        }
        ssize_t offset = 0;
        while (ok && offset < size)
        {
            ssize_t written = write(output, buffer + offset, (size_t)(size - offset));
            if (written < 0 && errno == EINTR) continue;
            ok = written > 0;
            if (ok) offset += written;
        }
    }
    if (output >= 0 && fchmod(output, 0400) != 0) ok = false;
    if (output >= 0 && close(output) != 0) ok = false;
    if (input >= 0 && close(input) != 0) ok = false;
    return ok;
}

static int frozen_runtime_log(int directory, char const* name, char const* contents)
{
    int output = openat(directory, name, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    size_t bytes = strlen(contents);
    bool ok = output >= 0 && write(output, contents, bytes) == (ssize_t)bytes &&
              fchmod(output, 0400) == 0;
    if (output >= 0 && close(output) != 0) ok = false;
    int result = ok ? openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    return result;
}

static int wait_artifact_output(BqRetirementArtifactStart* start)
{
    int result = 0;
    for (unsigned attempt = 0; !result && attempt < 10000; attempt += 1)
    {
        result = bq_retirement_artifact_poll(start);
        if (!result)
        {
            struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
    return result;
}

static int wait_runtime_output(BqRetirementRuntimeStart* start)
{
    int result = 0;
    for (unsigned attempt = 0; !result && attempt < 10000; attempt += 1)
    {
        result = bq_retirement_runtime_poll(start);
        if (!result)
        {
            struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
    return result;
}

static void test_frozen_artifact_readback(char const* executable)
{
    char root[] = "/tmp/bq-retirement-artifact-XXXXXX";
    bool created = mkdtemp(root) != NULL;
    int directory = created ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    CHECK(directory >= 0);
    if (directory >= 0)
    {
        char process_path[4096];
        char const* process_executable = executable;
        if (executable[0] != '/')
        {
            char* cwd = getcwd(process_path, sizeof(process_path));
            bool fits = cwd && strlen(process_path) + 1 + strlen(executable) < sizeof(process_path);
            CHECK(fits);
            if (fits)
            {
                size_t prefix = strlen(process_path);
                process_path[prefix] = '/';
                memcpy(process_path + prefix + 1, executable, strlen(executable) + 1);
                process_executable = process_path;
            }
        }
        char const* oracle_output = "independent-oracle-output\n";
        char oracle_sha256[65];
        Sha256 oracle_hash;
        sha256_init(&oracle_hash);
        sha256_add(&oracle_hash, oracle_output, strlen(oracle_output));
        sha256_finish_hex(&oracle_hash, oracle_sha256);
        int wrong_log = frozen_runtime_log(directory, "wrong-output", "wrong-output\n");
        int empty_log = frozen_runtime_log(directory, "empty-output", "");
        char empty_digest[65];
        CHECK(wrong_log >= 3 && empty_log >= 3 &&
              bq_retirement_runtime_read(empty_log, empty_digest) &&
              !strcmp(empty_digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
        BqRetirementArtifactStart stale_artifact = {0};
        BqRetirementRuntimeStart stale_runtime = {0};
        BqRetirementArtifactLocation stale = {directory, "wrong-output"};
        CHECK(!bq_retirement_artifact_start(stale, &stale_artifact) &&
              !bq_retirement_runtime_start(stale, &stale_runtime) &&
              !stale_artifact.armed && !stale_runtime.state);
        CHECK(!bq_retirement_artifact_start(
            (BqRetirementArtifactLocation){directory, "../base"}, &stale_artifact));
        BqRetirementArtifactLocation reserved = {directory, "reserved-artifact"};
        CHECK(bq_retirement_artifact_start(reserved, &stale_artifact) &&
              !bq_retirement_artifact_start(
                  (BqRetirementArtifactLocation){directory, "other-artifact"}, &stale_artifact) &&
              stale_artifact.armed && !strcmp(stale_artifact.name, "reserved-artifact"));
        BqRetirementArtifactLocation active_log = {directory, "active-output"};
        CHECK(bq_retirement_runtime_start(active_log, &stale_runtime) &&
              !bq_retirement_runtime_start(
                  (BqRetirementArtifactLocation){directory, "unexpected-output"}, &stale_runtime) &&
              stale_runtime.state == BQ_RETIREMENT_RUNTIME_CREATED);
        bq_retirement_runtime_abort(&stale_runtime);
        CHECK(bq_retirement_runtime_absent(&stale_runtime) &&
              unlinkat(directory, "active-output", 0) == 0);
#if defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
        unsigned host_target = 2, other_machine = 8;
#else
        unsigned host_target = 8, other_machine = 2;
#endif
#elif defined(__aarch64__) || defined(__arm64__)
        unsigned host_target = 5, other_machine = 11;
#else
        unsigned host_target = 11, other_machine = 5;
#endif
        BqRetirementArtifactLocation locations[2] = {{directory, "base"}, {directory, "candidate"}};
        char* const base_compiler[] = {(char*)process_executable,
            "--retirement-copy-self", "base", NULL};
        char* const candidate_compiler[] = {(char*)process_executable,
            "--retirement-copy-self", "candidate", NULL};
        char* const changed_compiler[] = {"/trusted/candidate-ide", "-O2", "input.c", NULL};
        char* const runtime_arguments[] = {(char*)process_executable,
            "--retirement-oracle-output", NULL};
        char* const changed_runtime[] = {"/scratch/other",
            "--retirement-oracle-output", NULL};
        char* const environment[] = {"HOME=/nonexistent", "LC_ALL=C", NULL};
        char* const unordered_environment[] = {"LC_ALL=C", "HOME=/nonexistent", NULL};
        BqRetirementRowCommands commands[2] = {
            {{base_compiler, environment, root, 3, 2},
             {runtime_arguments, environment, root, 2, 2}},
            {{candidate_compiler, environment, root, 3, 2},
             {runtime_arguments, environment, root, 2, 2}}
        };
        BqRetirementRuntimeStart unlaunched = {0};
        BqRetirementArtifactLocation no_launch = {directory, "no-launch-output"};
        int no_launch_reader = -1;
        CHECK(bq_retirement_runtime_start(no_launch, &unlaunched));
        CHECK(!bq_retirement_runtime_finish(&unlaunched, &no_launch_reader) &&
              no_launch_reader == -1 && bq_retirement_runtime_absent(&unlaunched));
        CHECK(unlinkat(directory, "no-launch-output", 0) == 0);
        BqRetirementRuntimeStart failed = {0};
        BqRetirementArtifactLocation failed_output = {directory, "failed-output"};
        char* const failed_arguments[] = {(char*)process_executable,
            "--retirement-oracle-fail", NULL};
        BqRetirementProcessCommand failed_command = {
            failed_arguments, environment, root, 2, 2
        };
        int failed_reader = -1;
        CHECK(bq_retirement_runtime_start(failed_output, &failed));
        CHECK(bq_retirement_runtime_launch(&failed, &failed_command));
        CHECK(!bq_retirement_runtime_finish(&failed, &failed_reader) &&
              failed_reader == -1 && failed.state == BQ_RETIREMENT_RUNTIME_RUNNING);
        bq_retirement_runtime_abort(&failed);
        CHECK(failed.state == BQ_RETIREMENT_RUNTIME_RUNNING);
        CHECK(wait_runtime_output(&failed) == -1 && failed.state == BQ_RETIREMENT_RUNTIME_FAILED);
        CHECK(!bq_retirement_runtime_finish(&failed, &failed_reader) &&
              bq_retirement_runtime_absent(&failed));
        CHECK(unlinkat(directory, "failed-output", 0) == 0);
        BqRetirementArtifactStart failed_compiler = {0};
        BqRetirementArtifactLocation failed_artifact = {directory, "failed-artifact"};
        char* const failed_compiler_arguments[] = {(char*)process_executable,
            "--retirement-oracle-fail", NULL};
        BqRetirementProcessCommand failed_compiler_command = {
            failed_compiler_arguments, environment, root, 2, 2
        };
        CHECK(bq_retirement_artifact_start(failed_artifact, &failed_compiler) &&
              bq_retirement_artifact_launch(&failed_compiler, &failed_compiler_command));
        CHECK(wait_artifact_output(&failed_compiler) == -1 &&
              failed_compiler.process_state == BQ_RETIREMENT_ARTIFACT_FAILED);
        bq_retirement_artifact_abort(&failed_compiler);
        CHECK(bq_retirement_output_absent(&failed_compiler));
        for (unsigned fault = 0; fault < 21; fault += 1)
        {
            BqRetirementArtifactStart starts[2] = {0};
            BqRetirementRuntimeStart runtime_starts[2] = {0};
            BqRetirementArtifactLocation runtime_locations[2] = {
                {directory, "base-output"}, {directory, "candidate-output"}
            };
            BqRetirementArtifactLocation candidate_start = {
                directory, fault == 1 ? "linked" : "candidate"
            };
            CHECK(bq_retirement_artifact_start(locations[0], &starts[0]) &&
                  bq_retirement_artifact_start(candidate_start, &starts[1]));
            for (unsigned side = 0; side < 2; side += 1)
            {
                if (fault == 20 && side == 1)
                    CHECK(copy_frozen_artifact(directory, executable, "candidate"));
                else
                {
                    CHECK(bq_retirement_artifact_launch(&starts[side],
                        &commands[side].compiler));
                    CHECK(wait_artifact_output(&starts[side]) == 1);
                }
            }
            CHECK(symlinkat("base", directory, "linked") == 0);
            int runtime_outputs[2] = {-1, -1};
            for (unsigned side = 0; side < 2; side += 1)
            {
                CHECK(bq_retirement_runtime_start(runtime_locations[side],
                    &runtime_starts[side]));
                CHECK(bq_retirement_runtime_launch(&runtime_starts[side],
                    &commands[side].runtime));
                CHECK(wait_runtime_output(&runtime_starts[side]) == 1);
                CHECK(bq_retirement_runtime_finish(&runtime_starts[side],
                    &runtime_outputs[side]));
            }
            BqCorrectnessFixture fixture;
            fixture_init(&fixture);
            memcpy(fixture.trusted[1].independent_oracle_sha256, oracle_sha256, 65);
            for (unsigned side = 0; side < 2; side += 1)
            {
                CHECK(bq_retirement_command_hash(&commands[side].compiler,
                    fixture.trusted[1].compiler_command_sha256[side]) &&
                    bq_retirement_command_hash(&commands[side].runtime,
                    fixture.trusted[1].runtime_command_sha256[side]));
            }
            CHECK(!strcmp(fixture.trusted[1].compiler_command_sha256[0],
                starts[0].command_sha256));
            unsigned target = fault == 5 ? other_machine : host_target;
            fixture.prepared.native_target = target;
            for (unsigned i = 0; i < BQ_TEST_ROWS; i += 1)
                fixture.trusted[i].target = i == 4 ? (target == 1 ? 2 : 1) : target;
            begin_fixture(&fixture);
            checks_fixture(&fixture);
            CHECK(bq_retirement_correctness_row(&fixture.gate, &fixture.supplied[0]));
            BqRetirementRowFact observed = fixture.supplied[1];
            for (unsigned side = 0; side < 2; side += 1)
            {
                observed.side[side].artifact_sha256[0] = 0;
                observed.side[side].code_sha256[0] = 0;
                observed.side[side].code_bytes = 0;
                observed.side[side].runtime_output_sha256[0] = 0;
                observed.side[side].compiler_command_sha256[0] = 0;
                observed.side[side].runtime_command_sha256[0] = 0;
            }
            observed.code_eligible = 0;
            if (fault == 1) locations[1].name = "linked";
            if (fault == 2) locations[1].name = "../base";
            if (fault == 3) observed.side[0].code_sha256[0] = 'a';
            if (fault == 4) CHECK(fchmodat(directory, "candidate", 0600, 0) == 0);
            if (fault == 8) observed.side[0].runtime_output_sha256[0] = 'a';
            if (fault == 9) CHECK(fchmodat(directory, "candidate-output", 0600, 0) == 0);
            if (fault == 10) commands[1].compiler.arguments = changed_compiler;
            if (fault == 11) commands[1].compiler.environment = unordered_environment;
            if (fault == 12) commands[1].runtime.arguments = changed_runtime;
            if (fault == 13) observed.side[0].compiler_command_sha256[0] = 'a';
            if (fault == 14) commands[1].compiler.directory = "/source/other";
            if (fault == 15) starts[1].directory_inode ^= 1;
            if (fault == 16) runtime_starts[1].file_inode ^= 1;
            if (fault == 18) runtime_starts[1].command_sha256[0] ^= 1;
            if (fault == 19) starts[1].command_sha256[0] ^= 1;
            if (fault == 17)
            {
                CHECK(renameat(directory, "candidate-output", directory, "displaced-output") == 0);
                int replacement = frozen_runtime_log(directory, "candidate-output", oracle_output);
                CHECK(replacement >= 3);
                if (replacement >= 3) CHECK(close(replacement) == 0);
            }
            int candidate_log = runtime_outputs[1];
            if (fault == 6) runtime_outputs[1] = wrong_log;
            if (fault == 7) runtime_outputs[1] = -1;
            bool accepted = bq_retirement_correctness_row_service(&fixture.gate, locations,
                starts, runtime_outputs, runtime_starts, commands, &observed);
            CHECK(accepted == (fault == 0));
            if (!fault)
            {
                CHECK(fixture.facts[1].side[0].code_bytes > 0 &&
                      fixture.facts[1].side[1].code_bytes > 0 &&
                      fixture.facts[1].code_eligible &&
                      !strcmp(fixture.facts[1].side[0].runtime_output_sha256, oracle_sha256) &&
                      !strcmp(fixture.facts[1].side[1].runtime_output_sha256, oracle_sha256) &&
                      strcmp(fixture.facts[1].side[0].artifact_sha256,
                             fixture.facts[1].side[0].code_sha256));
                for (unsigned i = 2; i < BQ_TEST_ROWS; i += 1)
                {
                    if (i == 3)
                    {
                        BqRetirementArtifactLocation empty[2] = {{directory, NULL}, {directory, NULL}};
                        BqRetirementArtifactStart empty_starts[2] = {0};
                        int absent_logs[2] = {-1, -1};
                        BqRetirementRuntimeStart absent_runtime[2] = {0};
                        BqRetirementRowCommands absent_commands[2] = {0};
                        CHECK(bq_retirement_correctness_row_service(&fixture.gate, empty,
                            empty_starts, absent_logs, absent_runtime, absent_commands,
                            &fixture.supplied[i]));
                    }
                    else CHECK(bq_retirement_correctness_row(&fixture.gate, &fixture.supplied[i]));
                }
                CHECK(bq_retirement_correctness_finish(&fixture.gate) &&
                      bq_retirement_correctness_ready(&fixture.gate));
            }
            else CHECK(fixture.gate.failed && !bq_retirement_correctness_ready(&fixture.gate));
            locations[1].name = "candidate";
            if (fault == 4) CHECK(fchmodat(directory, "candidate", 0400, 0) == 0);
            if (fault == 9) CHECK(fchmodat(directory, "candidate-output", 0400, 0) == 0);
            commands[1].compiler.arguments = candidate_compiler;
            commands[1].compiler.environment = environment;
            commands[1].compiler.directory = root;
            commands[1].runtime.arguments = runtime_arguments;
            CHECK(bq_retirement_output_absent(&starts[0]) &&
                  bq_retirement_output_absent(&starts[1]) &&
                  bq_retirement_runtime_absent(&runtime_starts[0]) &&
                  bq_retirement_runtime_absent(&runtime_starts[1]));
            CHECK(close(runtime_outputs[0]) == 0 && close(candidate_log) == 0 &&
                  unlinkat(directory, "linked", 0) == 0 &&
                  unlinkat(directory, "base", 0) == 0 &&
                  unlinkat(directory, "candidate", 0) == 0 &&
                  unlinkat(directory, "base-output", 0) == 0 &&
                  unlinkat(directory, "candidate-output", 0) == 0);
            if (fault == 17) CHECK(unlinkat(directory, "displaced-output", 0) == 0);
        }
        BqCorrectnessFixture untimed;
        fixture_init(&untimed);
        begin_fixture(&untimed);
        checks_fixture(&untimed);
        for (unsigned i = 0; i < 3; i += 1)
            CHECK(bq_retirement_correctness_row(&untimed.gate, &untimed.supplied[i]));
        BqRetirementArtifactLocation absent_artifacts[2] = {{directory, NULL}, {directory, NULL}};
        BqRetirementArtifactStart absent_starts[2] = {0};
        int absent_logs[2] = {-1, -1};
        BqRetirementRuntimeStart absent_runtime[2] = {0};
        BqRetirementRowCommands unexpected[2] = {0};
        unexpected[0].compiler = commands[0].compiler;
        CHECK(!bq_retirement_correctness_row_service(&untimed.gate, absent_artifacts,
            absent_starts, absent_logs, absent_runtime, unexpected,
            &untimed.supplied[3]) && untimed.gate.failed);
        fixture_init(&untimed);
        begin_fixture(&untimed);
        checks_fixture(&untimed);
        for (unsigned i = 0; i < 3; i += 1)
            CHECK(bq_retirement_correctness_row(&untimed.gate, &untimed.supplied[i]));
        unexpected[0] = (BqRetirementRowCommands){0};
        absent_starts[0].directory_inode = 1;
        CHECK(!bq_retirement_correctness_row_service(&untimed.gate, absent_artifacts,
            absent_starts, absent_logs, absent_runtime, unexpected,
            &untimed.supplied[3]) && untimed.gate.failed);
        CHECK(close(wrong_log) == 0 && close(empty_log) == 0 &&
              unlinkat(directory, "wrong-output", 0) == 0 &&
              unlinkat(directory, "empty-output", 0) == 0);
        CHECK(close(directory) == 0);
    }
    if (created) CHECK(rmdir(root) == 0);
}

int main(int argc, char** argv)
{
    int result = 0;
    if (argc == 2 && !strcmp(argv[1], "--retirement-oracle-output"))
        result = fputs("independent-oracle-output\n", stdout) < 0 || fflush(stdout) != 0;
    else if (argc == 2 && !strcmp(argv[1], "--retirement-oracle-fail")) result = 7;
    else if (argc == 3 && !strcmp(argv[1], "--retirement-copy-self"))
    {
        int directory = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        result = directory >= 0 && copy_frozen_artifact(directory, argv[0], argv[2]) ? 0 : 6;
        if (directory >= 0 && close(directory) != 0) result = 6;
    }
    else
    {
        test_valid();
        test_bad_checks();
        test_bad_rows();
        test_bad_import();
        test_partial_and_sealed();
        test_large_population();
        if (argc > 0) test_frozen_artifact_readback(argv[0]);
        printf("RETIREMENT_CORRECTNESS_TEST assertions=%u failures=%u launches=%u\n",
               assertions, failures, launches);
        result = failures ? 1 : 0;
    }
    return result;
}
