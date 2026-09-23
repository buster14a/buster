/* Standalone miniature producer fixture for #1020. The integration owner
 * registers this file in the shared service/hosted test graph after #1018's
 * importer and the production pre-timing boundary are wired.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/buster/lib/hash.c"
#define BUSTER_RETIREMENT_CORRECTNESS_FIXTURE 1
#include "retirement_correctness.c"

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
    fixture.required[0].command_sha256[0] = 'f';
    CHECK(!bq_retirement_correctness_ready(&fixture.gate));
    fixture.required[0].command_sha256[0] = '3';
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

static void test_v1_population(void)
{
    CHECK(bq_retirement_correctness_population(78912, 72672));
    CHECK(!bq_retirement_correctness_population(78911, 72672));
    CHECK(!bq_retirement_correctness_population(78912, 72671));
    CHECK(!bq_retirement_correctness_population(78913, 72672));
    CHECK(!bq_retirement_correctness_population(78912, 72673));
}

static void test_bad_checks(void)
{
    for (unsigned fault = 0; fault < 9; fault += 1)
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
        CHECK(!bq_retirement_correctness_check(&fixture.gate, check));
        CHECK(!bq_retirement_correctness_check(&fixture.gate, &fixture.checks[1]));
        launch_if_ready(&fixture.gate);
        CHECK(launches == 1);
    }
}

static void test_bad_rows(void)
{
    for (unsigned fault = 0; fault < 12; fault += 1)
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
        CHECK(!bq_retirement_correctness_row(&fixture.gate, row));
        CHECK(!bq_retirement_correctness_finish(&fixture.gate));
        launch_if_ready(&fixture.gate);
        CHECK(launches == 1);
    }
    for (unsigned fault = 0; fault < 5; fault += 1)
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
        CHECK(!bq_retirement_correctness_row(&fixture.gate, row));
        launch_if_ready(&fixture.gate);
        CHECK(launches == 1);
    }
}

static void test_bad_import(void)
{
    for (unsigned fault = 0; fault < 11; fault += 1)
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

int main(void)
{
    test_v1_population();
    test_valid();
    test_bad_checks();
    test_bad_rows();
    test_bad_import();
    test_partial_and_sealed();
    test_large_population();
    printf("RETIREMENT_CORRECTNESS_TEST assertions=%u failures=%u launches=%u\n",
           assertions, failures, launches);
    return failures ? 1 : 0;
}
