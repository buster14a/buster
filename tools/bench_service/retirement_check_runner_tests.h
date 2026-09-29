/* #1020/#509 lane B step 9 fixtures. Included by the preparation test runner
 * after the campaign fixture and before the unit-oracle fixture, which uses
 * bq_check_test_install and bq_check_test_evidence to drive the pinned gate,
 * the ready record and the replay on the real attempt.
 *
 * bq_check_test_runner covers, on a synthetic six-row population with the
 * A1 native-host target: the installed required-check authority (a pinned
 * canonical file and a pinned copy of the host shell as its one tool), the
 * in-unit runner with real children (pass, failing exit, per-check timeout,
 * an unrequested SIGKILL counted as OOM, wrong output, a lingering
 * descendant, cancellation, job deadline, planted evidence) and the
 * correctness half of the issuer (bq_retirement_unit_gate_admit), which must
 * refuse receipts from another job or token, a swapped binary and a missing
 * check. The shell and the two generated stand-in binaries prove mechanics
 * only; they are not #509 checks, and no full-corpus or #509 acceptance
 * follows from them. Nothing but the resolved host shell is taken from the
 * host: /usr/bin/true and /usr/bin/false may be symlinks into one multicall
 * binary (uutils coreutils), which the no-follow copy refuses and which
 * would not give two distinct binaries.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CHECK_RUNNER_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CHECK_RUNNER_TESTS_H

/* The unit's pinned native target is lane D's timed target. */
BUSTER_CT_CHECK(BQ_RETIREMENT_UNIT_NATIVE_TARGET == BQ_RETIREMENT_NATIVE_TIMED_TARGET);

#define BQ_CHECK_TEST_ROWS 6u
#define BQ_CHECK_TEST_OBJECT_ROWS 4u
#define BQ_CHECK_TEST_AUTHORITY_CAP 65536u
#define BQ_CHECK_TEST_CHECKS 11u

/* sha256 of a label and two numbers. */
BUSTER_GLOBAL_LOCAL void bq_check_test_digest(char digest[SHA256_HEX_CAPACITY], char const* label, u32 first,
    u32 second)
{
    char text[128];
    int length = snprintf(text, sizeof(text), "%s/%u/%u", label, first, second);
    bq_digest(text, length > 0 ? (u32)length : 0, (char8*)digest);
}

typedef struct BqCheckTestSpec
{
    char const* kind;
    u32 target, rows;
    char const* evidence;
    u32 timeout;
    /* A dash script: $0 is check-<i>, $1 the named binary, $2 A's base root;
     * the working directory is the check's work directory ($WORK). */
    char const* script;
    u32 binary;
} BqCheckTestSpec;

/* The eleven passing checks: census, the six #509 native semantic hosts
 * (native on the unit's target, honestly compile-only elsewhere), matrix,
 * no-fallback, self-host and fixed-point. rows are filled in per projection. */
BUSTER_GLOBAL_LOCAL void bq_check_test_specs(BqCheckTestSpec specs[BQ_CHECK_TEST_CHECKS], u32 object_rows,
    u32 eligible)
{
    static char const passing[] = "test -r \"$1\" && test -d \"$2\" && : > marker && test -f \"$WORK/marker\" && "
                                  "printf '%s ok\\n' \"$0\"";
    static char const* const kinds[BQ_CHECK_TEST_CHECKS] = {"census", "semantic", "semantic", "semantic", "semantic",
        "semantic", "semantic", "matrix", "no-fallback", "self-host", "fixed-point"};
    static u32 const targets[BQ_CHECK_TEST_CHECKS] = {0, 11, 5, 8, 2, 10, 4, 11, 0, 0, 0};
    for (u32 index = 0; index < BQ_CHECK_TEST_CHECKS; index += 1)
    {
        bool foreign = targets[index] && targets[index] != BQ_RETIREMENT_UNIT_NATIVE_TARGET;
        u32 rows = index == 0 ? object_rows : index == 7 || index == 8 ? eligible : 1;
        specs[index] = (BqCheckTestSpec){kinds[index], targets[index], rows, foreign ? "compile-only" : "native", 30,
                                         passing, index & 1u};
    }
}

/* The canonical authority text for specs over projection, with one tool. */
BUSTER_GLOBAL_LOCAL u32 bq_check_test_authority(char* text, BqRetirementProjection const* projection,
    char const tool_sha256[SHA256_HEX_CAPACITY], BqCheckTestSpec const* specs, u32 count)
{
    BqRetirementPrepared const* prepared = &projection->prepared;
    int used = snprintf(text, BQ_CHECK_TEST_AUTHORITY_CAP, "BQ-RETIREMENT-REQUIRED-CHECKS-V1\nsupport=%s\ncensus=%s\n"
                        "population=%s\nnative-target=%u\ntools=1\ntool=0 %s sh\nchecks=%u\n", prepared->support_sha256,
                        prepared->census_sha256, projection->population_sha256, prepared->native_target, tool_sha256,
                        count);
    bool ok = used > 0 && (u32)used < BQ_CHECK_TEST_AUTHORITY_CAP;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqCheckTestSpec const* spec = specs + index;
        char output[64], output_sha256[SHA256_HEX_CAPACITY];
        int output_length = snprintf(output, sizeof(output), "check-%u ok\n", index);
        bq_digest(output, (u32)output_length, (char8*)output_sha256);
        int line = snprintf(text + used, BQ_CHECK_TEST_AUTHORITY_CAP - (u32)used,
            "check=%u %s %u %u %s %u 1024 %s\nconfiguration=fixture %s %u\nargv=6\narg={{tool:0}}\narg=-c\narg=%s\n"
            "arg=check-%u\narg={{binary:%u}}\narg={{source:0}}\nenvironment=3\nenv=LC_ALL=C\nenv=PATH=/usr/bin:/bin\n"
            "env=WORK={{work}}\n", index, spec->kind, spec->target, spec->rows, spec->evidence, spec->timeout,
            output_sha256, spec->kind, spec->target, spec->script, index, spec->binary);
        ok = line > 0 && (u32)line < BQ_CHECK_TEST_AUTHORITY_CAP - (u32)used;
        if (ok) used += line;
    }
    return ok ? (u32)used : 0;
}

/* Installs (or replaces) the authority under recipes/ beside a pinned copy
 * of the host shell, and writes a profile whose only line pins it. */
BUSTER_GLOBAL_LOCAL bool bq_check_test_install(char const* recipes, BqRetirementProjection const* projection,
    BqCheckTestSpec const* specs, u32 count, char* profile, u32 profile_capacity)
{
    char tools[256], tool[320], authority[256], tool_sha256[SHA256_HEX_CAPACITY] = {0};
    char shell[4096];
    char* text = malloc(BQ_CHECK_TEST_AUTHORITY_CAP);
    int lengths[3] = {snprintf(tools, sizeof(tools), "%s/" BQ_RETIREMENT_CHECK_TOOLS_DIRECTORY, recipes),
                      snprintf(tool, sizeof(tool), "%s/sh", tools),
                      snprintf(authority, sizeof(authority), "%s/" BQ_RETIREMENT_REQUIRED_CHECKS_NAME, recipes)};
    bool ok = text && lengths[0] > 0 && (size_t)lengths[0] < sizeof(tools) && lengths[1] > 0 &&
              (size_t)lengths[1] < sizeof(tool) && lengths[2] > 0 && (size_t)lengths[2] < sizeof(authority) &&
              realpath("/bin/sh", shell) && chmod(recipes, 0700) == 0;
    struct stat info = {0};
    if (ok && stat(tools, &info) != 0)
        ok = mkdir(tools, 0700) == 0 && bq_prep_test_copy_file(shell, tool) && chmod(tools, 0555) == 0;
    int file = ok ? open(tool, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && file >= 3 && bq_retirement_oracle_file_hash(file, BQ_RETIREMENT_CHECK_TOOL_BYTES_CAP, true, tool_sha256);
    if (file >= 0) close(file);
    u32 length = ok ? bq_check_test_authority(text, projection, tool_sha256, specs, count) : 0;
    ok = ok && length && (unlink(authority) == 0 || errno == ENOENT) &&
         bq_prep_test_write_bytes(authority, text, length) && chmod(recipes, 0500) == 0;
    char digest[SHA256_HEX_CAPACITY] = {0};
    if (ok) bq_digest(text, length, (char8*)digest);
    int written = ok ? snprintf(profile, profile_capacity, "required-checks-sha256=%s\n", digest) : -1;
    free(text);
    return ok && written > 0 && (u32)written < profile_capacity;
}

/* The per-row half an honest row plan and runner would hand the gate:
 * commands for every compiler-eligible row, one frozen batch group holding
 * every timed object row, runtime commands and oracle outputs for native
 * runtime rows, and observed facts that pass. */
typedef struct BqCheckTestEvidence
{
    BqRetirementRowEvidence evidence;
    BqRetirementBatchGroup group;
    BqRetirementTrustedRow* rows;
    BqRetirementRowFact* facts;
    TpRetirementBatchInput* inputs;
    char (*names)[2][32];
    char object_sha256[SHA256_HEX_CAPACITY], diagnostic_sha256[SHA256_HEX_CAPACITY];
} BqCheckTestEvidence;

BUSTER_GLOBAL_LOCAL void bq_check_test_evidence_release(BqCheckTestEvidence* evidence)
{
    free(evidence->rows);
    free(evidence->facts);
    free(evidence->inputs);
    free(evidence->names);
    *evidence = (BqCheckTestEvidence){0};
}

BUSTER_GLOBAL_LOCAL bool bq_check_test_evidence(BqRetirementProjection const* projection, BqCheckTestEvidence* out)
{
    *out = (BqCheckTestEvidence){0};
    u32 count = projection->prepared.rows, native = projection->prepared.native_target, members = 0;
    out->rows = malloc((size_t)count * sizeof(*out->rows));
    out->facts = calloc(count, sizeof(*out->facts));
    out->inputs = calloc(count, sizeof(*out->inputs));
    out->names = calloc(count, sizeof(*out->names));
    bool ok = out->rows && out->facts && out->inputs && out->names;
    if (ok) memcpy(out->rows, projection->rows, (size_t)count * sizeof(*out->rows));
    char batch[2][SHA256_HEX_CAPACITY], key[SHA256_HEX_CAPACITY];
    bq_check_test_digest(batch[0], "batch-command", 0, 0);
    bq_check_test_digest(batch[1], "batch-command", 1, 0);
    bq_check_test_digest(key, "batch-key", 0, 0);
    bq_check_test_digest(out->object_sha256, "object", 0, 0);
    bq_digest("", 0, (char8*)out->diagnostic_sha256);
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqRetirementTrustedRow* row = out->rows + index;
        BqRetirementRowFact* fact = out->facts + index;
        bool compiler = row->compiler_eligible != 0;
        bool runtime = compiler && row->execution_obligation && row->stage != BQ_RETIREMENT_STAGE_OBJECT &&
                       row->target == native;
        bool timed = compiler && row->stage == BQ_RETIREMENT_STAGE_OBJECT && row->target == native;
        *fact = (BqRetirementRowFact){.row = row->row, .census_row = row->census_row, .compiler_eligible = compiler,
                                      .runtime_eligible = runtime};
        for (u32 side = 0; compiler && side < 2; side += 1)
        {
            BqRetirementObservedSide* observed = fact->side + side;
            if (timed) memcpy(row->compiler_command_sha256[side], batch[side], SHA256_HEX_CAPACITY);
            else bq_check_test_digest(row->compiler_command_sha256[side], "row-command", index, side);
            memcpy(observed->compiler_command_sha256, row->compiler_command_sha256[side], SHA256_HEX_CAPACITY);
            if (timed) memcpy(observed->artifact_sha256, out->object_sha256, SHA256_HEX_CAPACITY);
            else bq_check_test_digest(observed->artifact_sha256, "artifact", index, side);
            observed->semantic_pass = 1;
            if (row->code_obligation)
            {
                bq_check_test_digest(observed->code_sha256, "code", index, 0);
                observed->code_bytes = 16;
            }
            observed->runtime_exit = -1;
            if (runtime)
            {
                bq_check_test_digest(row->runtime_command_sha256[side], "runtime-command", index, side);
                memcpy(observed->runtime_command_sha256, row->runtime_command_sha256[side], SHA256_HEX_CAPACITY);
                memcpy(observed->runtime_output_sha256, row->independent_oracle_sha256, SHA256_HEX_CAPACITY);
                observed->runtime_exit = 0;
            }
        }
        fact->code_eligible = compiler && row->code_obligation && fact->side[0].code_bytes > 0;
        if (timed)
        {
            memcpy(row->batch_key_sha256, key, SHA256_HEX_CAPACITY);
            snprintf(out->names[members][0], sizeof(out->names[members][0]), "tests/row-%u.c", index);
            snprintf(out->names[members][1], sizeof(out->names[members][1]), "row-%u.o", index);
            out->inputs[members] = (TpRetirementBatchInput){out->names[members][0], "ok", "driver.none",
                out->diagnostic_sha256, out->object_sha256, out->names[members][1], 1, index};
            members += 1;
        }
    }
    for (u32 side = 0; side < 2; side += 1)
    {
        out->group.contract[side] = (TpRetirementBatchContract){"x86_64-linux", "none", "batch.metrics", out->inputs,
                                                                members, 0, 1u << 20};
        memcpy(out->group.command_sha256[side], batch[side], SHA256_HEX_CAPACITY);
    }
    out->evidence = (BqRetirementRowEvidence){out->rows, out->facts, members ? &out->group : NULL, count,
                                              members ? 1u : 0u, {0}, {0}};
    bq_check_test_digest(out->evidence.aa_second_commands_sha256, "aa-second", 0, 0);
    bq_check_test_digest(out->evidence.plan_sha256, "row-plan", 0, 0);
    return ok;
}

typedef struct BqCheckTestFixture
{
    char installed[64], workspaces[64], recipes[96];
    char profile[256];
    BqRetirementTrustedRow rows[BQ_CHECK_TEST_ROWS];
    BqRetirementProjection projection;
    BqJob job;
    BqRetirementHeldBinaries held;
    BqCheckTestSpec specs[BQ_CHECK_TEST_CHECKS];
    int installed_fd, workspaces_fd, sources[2], cancel[2];
    u32 directories;
} BqCheckTestFixture;

/* The six rows: two timed native objects, a foreign object, an ineligible
 * native object (census rows 0-3), a native link row with a runtime oracle
 * and a native self-host row. */
BUSTER_GLOBAL_LOCAL void bq_check_test_rows(BqCheckTestFixture* fixture)
{
    static u32 const census[BQ_CHECK_TEST_ROWS] = {0, 1, 2, 3, 0, 1};
    static u32 const targets[BQ_CHECK_TEST_ROWS] = {11, 11, 5, 11, 11, 11};
    static u32 const stages[BQ_CHECK_TEST_ROWS] = {BQ_RETIREMENT_STAGE_OBJECT, BQ_RETIREMENT_STAGE_OBJECT,
        BQ_RETIREMENT_STAGE_OBJECT, BQ_RETIREMENT_STAGE_OBJECT, BQ_RETIREMENT_STAGE_LINK, BQ_RETIREMENT_STAGE_SELF_HOST};
    for (u32 index = 0; index < BQ_CHECK_TEST_ROWS; index += 1)
    {
        BqRetirementTrustedRow* row = fixture->rows + index;
        *row = (BqRetirementTrustedRow){.row = index, .census_row = census[index], .target = targets[index],
            .stage = stages[index], .classification = index == 3 ? 2u : 1u, .compiler_eligible = index != 3,
            .code_obligation = index != 3, .execution_obligation = index == 4};
        bq_check_test_digest(row->identity_sha256, "identity", index, 0);
        bq_check_test_digest(row->source_sha256, "source", census[index], 0);
        bq_check_test_digest(row->configuration_sha256, "configuration", census[index], 0);
        if (index == 3) bq_check_test_digest(row->skip_proof_sha256, "skip", index, 0);
        if (index == 4) bq_check_test_digest(row->independent_oracle_sha256, "oracle", index, 0);
    }
}

/* A small executable stand-in with distinct bytes per status: a new,
 * single-link, owner read-and-execute file. */
BUSTER_GLOBAL_LOCAL bool bq_check_test_program(char const* path, u32 status)
{
    char text[64];
    int length = snprintf(text, sizeof(text), "#!/bin/sh\nexit %u\n", status);
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0700);
    bool ok = length > 0 && (size_t)length < sizeof(text) && file >= 0 &&
              bq_write_all(file, (u8 const*)text, (u32)length) && fchmod(file, 0500) == 0 && fsync(file) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_check_test_directory(BqCheckTestFixture* fixture, char const* prefix, int* descriptor)
{
    char name[48];
    int length = snprintf(name, sizeof(name), "%s-%u", prefix, fixture->directories++);
    bool ok = length > 0 && (size_t)length < sizeof(name) && mkdirat(fixture->workspaces_fd, name, 0700) == 0;
    *descriptor = ok ? bq_retirement_check_promote(openat(fixture->workspaces_fd, name,
                                                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) : -1;
    return ok && *descriptor >= 3;
}

BUSTER_GLOBAL_LOCAL bool bq_check_test_setup(BqCheckTestFixture* fixture)
{
    *fixture = (BqCheckTestFixture){.installed_fd = -1, .workspaces_fd = -1, .sources = {-1, -1}, .cancel = {-1, -1},
                                    .held = {.descriptors = {-1, -1}}};
    strcpy(fixture->installed, "/tmp/bq-check-installed-XXXXXX");
    strcpy(fixture->workspaces, "/tmp/bq-check-workspaces-XXXXXX");
    if (!mkdtemp(fixture->installed)) fixture->installed[0] = 0;
    if (!mkdtemp(fixture->workspaces)) fixture->workspaces[0] = 0;
    int length = snprintf(fixture->recipes, sizeof(fixture->recipes), "%s/recipes", fixture->installed);
    bool ok = fixture->installed[0] && fixture->workspaces[0] && length > 0 &&
              (size_t)length < sizeof(fixture->recipes) && mkdir(fixture->recipes, 0500) == 0 &&
              pipe2(fixture->cancel, O_CLOEXEC | O_NONBLOCK) == 0;
    fixture->installed_fd = ok ? open(fixture->installed, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    fixture->workspaces_fd = ok ? open(fixture->workspaces, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && fixture->installed_fd >= 3 && fixture->workspaces_fd >= 3 &&
         bq_check_test_directory(fixture, "source-base", &fixture->sources[0]) &&
         bq_check_test_directory(fixture, "source-candidate", &fixture->sources[1]);
    /* The last setup stage reached, reported on failure. */
    u32 stage = ok ? 1u : 0u;
    /* The two held matched binaries. */
    BqRetirementPrepared* prepared = &fixture->projection.prepared;
    for (u32 side = 0; ok && side < 2; side += 1)
    {
        char path[128];
        length = snprintf(path, sizeof(path), "%s/binary-%u", fixture->workspaces, side);
        ok = length > 0 && (size_t)length < sizeof(path) && bq_check_test_program(path, side);
        fixture->held.descriptors[side] = ok ? bq_retirement_check_promote(open(path, O_RDONLY | O_CLOEXEC)) : -1;
        ok = ok && fixture->held.descriptors[side] >= 3 &&
             bq_retirement_oracle_file_hash(fixture->held.descriptors[side], BQ_RETIREMENT_CHECK_TOOL_BYTES_CAP, true,
                                            prepared->binary_sha256[side]);
        bq_check_test_digest(prepared->source_sha256[side], "source-manifest", side, 0);
    }
    fixture->held.owned = ok;
    stage += ok;
    memcpy(fixture->held.verified.source_sha256, prepared->source_sha256, sizeof(prepared->source_sha256));
    memcpy(fixture->held.verified.binary_sha256, prepared->binary_sha256, sizeof(prepared->binary_sha256));
    bq_check_test_digest(prepared->preparation_sha256, "preparation", 0, 0);
    memcpy(fixture->held.verified.preparation_sha256, prepared->preparation_sha256, SHA256_HEX_CAPACITY);
    bq_check_test_digest(prepared->support_sha256, "support", 0, 0);
    bq_check_test_digest(prepared->census_sha256, "census", 0, 0);
    prepared->rows = BQ_CHECK_TEST_ROWS;
    prepared->object_rows = BQ_CHECK_TEST_OBJECT_ROWS;
    prepared->native_target = BQ_RETIREMENT_UNIT_NATIVE_TARGET;
    bq_check_test_rows(fixture);
    fixture->projection.rows = fixture->rows;
    fixture->projection.job_id = 91;
    fixture->projection.attempt_token = 5;
    fixture->projection.owned = 1;
    ok = ok && bq_retirement_oracle_population_hash(fixture->rows, BQ_CHECK_TEST_ROWS,
                                                    fixture->projection.population_sha256);
    String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("receipts"), S8("native-retirement-performance-v1"),
        S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")};
    /* The retirement recipe is unadmitted, so the request is encoded by hand
     * as the unit-oracle fixture does. */
    fixture->job = (BqJob){.id = 91, .token = 5};
    for (u32 index = 0; index < BQ_FIELD_COUNT; index += 1)
    {
        bq_put32(fixture->job.request.bytes + fixture->job.request.size, (u32)fields[index].length);
        fixture->job.request.size += 4;
        memcpy(fixture->job.request.bytes + fixture->job.request.size, fields[index].pointer,
               (size_t)fields[index].length);
        fixture->job.request.size += (u32)fields[index].length;
    }
    bq_request_digest(&fixture->job.request, fixture->job.digest);
    bq_check_test_specs(fixture->specs, BQ_CHECK_TEST_OBJECT_ROWS, BQ_CHECK_TEST_ROWS - 1u);
    stage += ok;
    ok = ok && bq_check_test_install(fixture->recipes, &fixture->projection, fixture->specs, BQ_CHECK_TEST_CHECKS,
                                     fixture->profile, sizeof(fixture->profile));
    if (!ok) fprintf(stderr, "RETIREMENT_PREP check fixture setup stopped after stage %u (errno %d)\n", stage, errno);
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_check_test_teardown(BqCheckTestFixture* fixture)
{
    if (fixture->held.owned) bq_retirement_binaries_release(&fixture->held);
    int const opened[] = {fixture->sources[0], fixture->sources[1], fixture->cancel[0], fixture->cancel[1],
                          fixture->installed_fd, fixture->workspaces_fd};
    for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(opened); slot += 1)
        if (opened[slot] >= 0) close(opened[slot]);
    if (fixture->workspaces[0]) bq_prep_test_cleanup(fixture->workspaces);
    if (fixture->installed[0]) bq_prep_test_cleanup(fixture->installed);
}

/* Imports with the fixture profile, or with the given specs reinstalled. */
BUSTER_GLOBAL_LOCAL BqError bq_check_test_import(BqCheckTestFixture* fixture, BqJob const* job,
    BqRetirementProjection const* projection, BqRetirementRequiredChecks* checks)
{
    BqError result = bq_retirement_required_checks_import_profile(fixture->installed_fd,
        string_from_pointer(fixture->profile), job, projection, checks);
    return result;
}

/* Runs every check of checks into fresh evidence and work directories. */
BUSTER_GLOBAL_LOCAL bool bq_check_test_run_all(BqCheckTestFixture* fixture, BqRetirementRequiredChecks const* checks,
    BqRetirementHeldBinaries const* held, BqRetirementCheckResult* results, int* evidence_out)
{
    int evidence = -1, work = -1;
    bool ok = bq_check_test_directory(fixture, "evidence", &evidence) && bq_check_test_directory(fixture, "work", &work);
    BqRetirementCheckRun run = {checks, held, {fixture->sources[0], fixture->sources[1]}, work, evidence,
                                fixture->cancel[0], bq_retirement_build_clock_ns() + 120ull * 1000000000ull};
    for (u32 index = 0; ok && index < checks->count; index += 1)
    {
        BqError result = bq_retirement_check_run(&run, index, results + index);
        if (result != BQ_OK) fprintf(stderr, "RETIREMENT_PREP check %u run returned %d\n", index, (int)result);
        ok = result == BQ_OK;
    }
    if (work >= 0) close(work);
    if (evidence_out) *evidence_out = evidence;
    else if (evidence >= 0) close(evidence);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_check_test_no_children(void)
{
    bool none = waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD;
    return none;
}

/* The authority importer: the pin, the file, coverage and labels. */
BUSTER_GLOBAL_LOCAL void bq_check_test_importer(BqCheckTestFixture* fixture)
{
    BqRetirementRequiredChecks checks = {0};
    /* The blocked profile has no pin; neither does a profile without the key. */
    BQ_PREP_CHECK(bq_retirement_required_checks_import(fixture->installed_fd, &fixture->job, &fixture->projection,
                  &checks) == BQ_RECIPE_MISMATCH && !checks.owned);
    BQ_PREP_CHECK(bq_retirement_required_checks_import_profile(fixture->installed_fd, S8("schema=1\n"), &fixture->job,
                  &fixture->projection, &checks) == BQ_RECIPE_MISMATCH && !checks.owned);
    char flipped[256];
    memcpy(flipped, fixture->profile, sizeof(flipped));
    bq_prep_test_flip_pin(flipped, "required-checks-sha256=");
    BQ_PREP_CHECK(bq_retirement_required_checks_import_profile(fixture->installed_fd, string_from_pointer(flipped),
                  &fixture->job, &fixture->projection, &checks) == BQ_RECIPE_MISMATCH && !checks.owned);
    /* The pinned authority, for this attempt. */
    BQ_PREP_CHECK(bq_check_test_import(fixture, &fixture->job, &fixture->projection, &checks) == BQ_OK &&
                  checks.owned && checks.count == BQ_CHECK_TEST_CHECKS && checks.tool_count == 1 &&
                  checks.tools[0] >= 3 && checks.job_id == 91 && checks.attempt_token == 5);
    for (u32 index = 0; checks.owned && index < checks.count; index += 1)
        BQ_PREP_CHECK(bq_retirement_hex(string_from_pointer(checks.checks[index].receipt_sha256), 64) &&
                      bq_retirement_hex(string_from_pointer(checks.checks[index].command_sha256), 64) &&
                      checks.checks[index].kind == checks.plans[index].kind);
    BqRetirementRequiredChecks again = {0};
    BQ_PREP_CHECK(bq_check_test_import(fixture, &fixture->job, &fixture->projection, &checks) == BQ_BAD_REQUEST &&
                  checks.owned);
    /* Another token of the same job expects other receipts. */
    BqJob other = fixture->job;
    other.token += 1;
    BqRetirementProjection moved = fixture->projection;
    moved.attempt_token = other.token;
    BQ_PREP_CHECK(bq_check_test_import(fixture, &other, &moved, &again) == BQ_OK &&
                  strcmp(again.checks[0].receipt_sha256, checks.checks[0].receipt_sha256) &&
                  !strcmp(again.checks[0].command_sha256, checks.checks[0].command_sha256));
    BQ_PREP_CHECK(bq_retirement_required_checks_release(&again) && bq_retirement_required_checks_release(&checks));
    /* A projection the authority does not name, or a changed sealed row. */
    moved = fixture->projection;
    moved.prepared.support_sha256[0] ^= 1;
    BQ_PREP_CHECK(bq_check_test_import(fixture, &fixture->job, &moved, &checks) == BQ_RECIPE_MISMATCH && !checks.owned);
    fixture->rows[2].configuration_sha256[0] ^= 1;
    BQ_PREP_CHECK(bq_check_test_import(fixture, &fixture->job, &fixture->projection, &checks) == BQ_SOURCE_MISMATCH);
    fixture->rows[2].configuration_sha256[0] ^= 1;

    /* Coverage and label violations, each reinstalled and re-pinned. */
    enum { DROP_KIND, DROP_HOST, FOREIGN_NATIVE, NATIVE_EMULATED, CENSUS_ROWS, MATRIX_ROWS, DUPLICATE, PATH_EXECUTABLE,
           BAD_TOKEN, TWO_TOKENS, UNSORTED_ENVIRONMENT, CASES };
    for (u32 variant = 0; variant < CASES; variant += 1)
    {
        BqCheckTestSpec specs[BQ_CHECK_TEST_CHECKS + 1u];
        memcpy(specs, fixture->specs, sizeof(fixture->specs));
        u32 count = BQ_CHECK_TEST_CHECKS;
        if (variant == DROP_KIND) count -= 1;
        if (variant == DROP_HOST) specs[6].target = 12;
        if (variant == FOREIGN_NATIVE) specs[2].evidence = "native";
        if (variant == NATIVE_EMULATED) specs[1].evidence = "emulated";
        if (variant == CENSUS_ROWS) specs[0].rows += 1;
        if (variant == MATRIX_ROWS) specs[7].rows -= 1;
        if (variant == DUPLICATE)
        {
            specs[BQ_CHECK_TEST_CHECKS] = specs[9];
            count += 1;
        }
        if (variant >= PATH_EXECUTABLE)
        {
            char profile[256];
            BQ_PREP_CHECK(bq_check_test_install(fixture->recipes, &fixture->projection, specs, count, profile,
                                                sizeof(profile)));
            /* Rewrite the one field after installation, then re-pin. */
            char authority[256], text[BQ_CHECK_TEST_AUTHORITY_CAP];
            snprintf(authority, sizeof(authority), "%s/" BQ_RETIREMENT_REQUIRED_CHECKS_NAME, fixture->recipes);
            u32 length = bq_prep_test_read_text(authority, text, sizeof(text));
            char const* needle = variant == PATH_EXECUTABLE ? "arg={{tool:0}}\n" :
                                 variant == UNSORTED_ENVIRONMENT ? "env=LC_ALL=C\nenv=PATH=/usr/bin:/bin\n" :
                                 "arg={{binary:1}}\n";
            char const* replacement = variant == PATH_EXECUTABLE ? "arg=/bin/sh\n" :
                                      variant == BAD_TOKEN ? "arg={{binary:2}}\n" :
                                      variant == UNSORTED_ENVIRONMENT ? "env=PATH=/usr/bin:/bin\nenv=LC_ALL=C\n" :
                                      "arg={{binary:1}}{{work}}\n";
            char* found = length ? strstr(text, needle) : NULL;
            char rewritten[BQ_CHECK_TEST_AUTHORITY_CAP];
            int written = found ? snprintf(rewritten, sizeof(rewritten), "%.*s%s%s", (int)(found - text), text,
                                           replacement, found + strlen(needle)) : -1;
            char digest[SHA256_HEX_CAPACITY] = {0};
            BQ_PREP_CHECK(written > 0 && (size_t)written < sizeof(rewritten) && chmod(fixture->recipes, 0700) == 0 &&
                          unlink(authority) == 0 && bq_prep_test_write_bytes(authority, rewritten, (u32)written) &&
                          chmod(fixture->recipes, 0500) == 0);
            bq_digest(rewritten, written > 0 ? (u32)written : 0, (char8*)digest);
            snprintf(profile, sizeof(profile), "required-checks-sha256=%s\n", digest);
            BQ_PREP_CHECK(bq_retirement_required_checks_import_profile(fixture->installed_fd, string_from_pointer(profile),
                          &fixture->job, &fixture->projection, &checks) == BQ_RECIPE_MISMATCH && !checks.owned);
        }
        else
        {
            char profile[256];
            BqError result = BQ_OK;
            BQ_PREP_CHECK(bq_check_test_install(fixture->recipes, &fixture->projection, specs, count, profile,
                                                sizeof(profile)));
            result = bq_retirement_required_checks_import_profile(fixture->installed_fd, string_from_pointer(profile),
                                                                  &fixture->job, &fixture->projection, &checks);
            if (result != BQ_RECIPE_MISMATCH) fprintf(stderr, "RETIREMENT_PREP check authority variant %u: %d\n", variant,
                                                      (int)result);
            BQ_PREP_CHECK(result == BQ_RECIPE_MISMATCH && !checks.owned);
        }
    }
    /* A tool whose bytes no longer match the authority's pin. */
    char tool[320], saved[320];
    snprintf(tool, sizeof(tool), "%s/" BQ_RETIREMENT_CHECK_TOOLS_DIRECTORY "/sh", fixture->recipes);
    snprintf(saved, sizeof(saved), "%s/" BQ_RETIREMENT_CHECK_TOOLS_DIRECTORY "/sh.saved", fixture->recipes);
    char tools[256];
    snprintf(tools, sizeof(tools), "%s/" BQ_RETIREMENT_CHECK_TOOLS_DIRECTORY, fixture->recipes);
    BQ_PREP_CHECK(bq_check_test_install(fixture->recipes, &fixture->projection, fixture->specs, BQ_CHECK_TEST_CHECKS,
                                        fixture->profile, sizeof(fixture->profile)) &&
                  chmod(tools, 0700) == 0 && rename(tool, saved) == 0 &&
                  bq_check_test_program(tool, 0) && chmod(tools, 0555) == 0);
    BQ_PREP_CHECK(bq_check_test_import(fixture, &fixture->job, &fixture->projection, &checks) ==
                  BQ_CONFIGURATION_MISMATCH && !checks.owned);
    BQ_PREP_CHECK(chmod(tools, 0700) == 0 && unlink(tool) == 0 && rename(saved, tool) == 0 && chmod(tools, 0555) == 0 &&
                  bq_check_test_import(fixture, &fixture->job, &fixture->projection, &checks) == BQ_OK &&
                  bq_retirement_required_checks_release(&checks));
}

/* The runner with real children. */
BUSTER_GLOBAL_LOCAL void bq_check_test_runs(BqCheckTestFixture* fixture)
{
    BqRetirementRequiredChecks checks = {0};
    BqRetirementCheckResult results[BQ_CHECK_TEST_CHECKS] = {0};
    int evidence = -1;
    BQ_PREP_CHECK(bq_check_test_import(fixture, &fixture->job, &fixture->projection, &checks) == BQ_OK);
    /* Every check passes: exit 0, the pinned output, this attempt's receipt
     * and the held binaries' digests; the evidence closes exactly. */
    BQ_PREP_CHECK(checks.owned && bq_check_test_run_all(fixture, &checks, &fixture->held, results, &evidence));
    for (u32 index = 0; checks.owned && index < checks.count; index += 1)
    {
        BqRetirementCheckResult const* result = results + index;
        BQ_PREP_CHECK(result->exit_code == 0 && !result->failures && !result->timed_out && !result->out_of_memory &&
                      !strcmp(result->receipt_sha256, checks.checks[index].receipt_sha256) &&
                      !strcmp(result->command_sha256, checks.checks[index].command_sha256) &&
                      !strcmp(result->binary_sha256[1], fixture->projection.prepared.binary_sha256[1]) &&
                      !strcmp(result->preparation_sha256, fixture->projection.prepared.preparation_sha256));
    }
    BQ_PREP_CHECK(bq_retirement_check_evidence_closed(evidence, checks.checks, checks.count, false) &&
                  !bq_retirement_check_evidence_closed(evidence, checks.checks, checks.count, true) &&
                  fchmod(evidence, BQ_RETIREMENT_EXPORT_MODE) == 0 &&
                  bq_retirement_check_evidence_closed(evidence, checks.checks, checks.count, true));
    /* A changed receipt, a missing file and an extra file break the closure. */
    char receipt[BQ_RETIREMENT_CHECK_RECEIPT_CAP + 1];
    int file = openat(evidence, "check-receipt-0003", O_RDONLY | O_CLOEXEC);
    ssize_t length = file >= 0 ? read(file, receipt, BQ_RETIREMENT_CHECK_RECEIPT_CAP) : -1;
    if (file >= 0) close(file);
    BQ_PREP_CHECK(length > 0 && fchmod(evidence, 0700) == 0 && unlinkat(evidence, "check-receipt-0003", 0) == 0);
    BQ_PREP_CHECK(!bq_retirement_check_evidence_closed(evidence, checks.checks, checks.count, false));
    receipt[length > 0 ? length - 2 : 0] ^= 1;
    file = openat(evidence, "check-receipt-0003", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0400);
    BQ_PREP_CHECK(file >= 0 && length > 0 && write(file, receipt, (size_t)length) == length && close(file) == 0);
    BQ_PREP_CHECK(!bq_retirement_check_evidence_closed(evidence, checks.checks, checks.count, false));
    receipt[length > 0 ? length - 2 : 0] ^= 1;
    BQ_PREP_CHECK(unlinkat(evidence, "check-receipt-0003", 0) == 0);
    file = openat(evidence, "check-receipt-0003", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0400);
    BQ_PREP_CHECK(file >= 0 && length > 0 && write(file, receipt, (size_t)length) == length && close(file) == 0 &&
                  bq_retirement_check_evidence_closed(evidence, checks.checks, checks.count, false));
    file = openat(evidence, "planted", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0400);
    BQ_PREP_CHECK(file >= 0 && close(file) == 0 &&
                  !bq_retirement_check_evidence_closed(evidence, checks.checks, checks.count, false) &&
                  unlinkat(evidence, "planted", 0) == 0);
    if (evidence >= 0) close(evidence);
    BQ_PREP_CHECK(bq_check_test_no_children());

    /* Failing children: a failing exit, the check's own wall bound, an
     * unrequested SIGKILL (counted as OOM), wrong output and a descendant
     * that outlives the check. Each is a verdict with a durable receipt. */
    static char const* const failing[] = {"exit 3", "exec sleep 30", "kill -KILL $$", "printf 'other\\n'",
                                          "sleep 5 & printf '%s ok\\n' \"$0\""};
    BqCheckTestSpec specs[BQ_CHECK_TEST_CHECKS];
    memcpy(specs, fixture->specs, sizeof(specs));
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(failing); index += 1) specs[index].script = failing[index];
    specs[1].timeout = 1;
    /* Two spinning checks for cancellation and the job deadline mid-run. */
    specs[5].script = "exec sleep 30";
    specs[6].script = "exec sleep 30";
    char profile[256];
    BqRetirementRequiredChecks broken = {0};
    BQ_PREP_CHECK(bq_check_test_install(fixture->recipes, &fixture->projection, specs, BQ_CHECK_TEST_CHECKS, profile,
                                        sizeof(profile)) &&
                  bq_retirement_required_checks_import_profile(fixture->installed_fd, string_from_pointer(profile),
                      &fixture->job, &fixture->projection, &broken) == BQ_OK);
    int work = -1;
    BQ_PREP_CHECK(bq_check_test_directory(fixture, "evidence", &evidence) &&
                  bq_check_test_directory(fixture, "work", &work));
    BqRetirementCheckRun run = {&broken, &fixture->held, {fixture->sources[0], fixture->sources[1]}, work, evidence,
                                fixture->cancel[0], bq_retirement_build_clock_ns() + 120ull * 1000000000ull};
    BqRetirementCheckResult observed[5] = {0};
    for (u32 index = 0; broken.owned && index < 5; index += 1)
        BQ_PREP_CHECK(bq_retirement_check_run(&run, index, observed + index) == BQ_OK &&
                      strcmp(observed[index].receipt_sha256, broken.checks[index].receipt_sha256));
    BQ_PREP_CHECK(observed[0].exit_code == 3 && !observed[0].timed_out && !observed[0].out_of_memory);
    BQ_PREP_CHECK(observed[1].timed_out == 1 && observed[1].exit_code == 128 + SIGKILL && !observed[1].out_of_memory);
    BQ_PREP_CHECK(observed[2].out_of_memory == 1 && observed[2].exit_code == 128 + SIGKILL && !observed[2].timed_out);
    BQ_PREP_CHECK(observed[3].exit_code == 0 && observed[3].failures == 1);
    BQ_PREP_CHECK(observed[4].exit_code == 0 && observed[4].failures == 1);
    BQ_PREP_CHECK(bq_check_test_no_children());
    /* The same check twice into one attempt: its names already exist. */
    BQ_PREP_CHECK(bq_retirement_check_run(&run, 0, observed) == BQ_WORKSPACE_MISMATCH);
    /* A readable cancellation descriptor and an expired job deadline stop a
     * run before any child; while a child spins, cancellation (the SIGTERM
     * self-pipe, written here by SIGALRM) and the job deadline kill its group
     * and reap it, and no receipt is written. */
    BQ_PREP_CHECK(write(fixture->cancel[1], "x", 1) == 1);
    BQ_PREP_CHECK(bq_retirement_check_run(&run, 5, observed) == BQ_WORKER_CANCEL_SIGNAL);
    char drained[4];
    BQ_PREP_CHECK(read(fixture->cancel[0], drained, sizeof(drained)) == 1);
    u64 generous = run.deadline_ns;
    run.deadline_ns = bq_retirement_build_clock_ns();
    BQ_PREP_CHECK(bq_retirement_check_run(&run, 5, observed) == BQ_WORKER_TIMEOUT);
    struct stat info = {0};
    run.deadline_ns = bq_retirement_build_clock_ns() + 1500000000ull;
    BQ_PREP_CHECK(bq_retirement_check_run(&run, 5, observed) == BQ_WORKER_TIMEOUT && bq_check_test_no_children() &&
                  fstatat(evidence, "check-receipt-0005", &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
    run.deadline_ns = generous;
    struct sigaction handler = {.sa_handler = bq_prep_test_cancel_handler}, prior = {0};
    struct itimerval timer = {.it_value = {1, 0}}, stopped = {{0, 0}, {0, 0}};
    bool armed = sigemptyset(&handler.sa_mask) == 0 && sigaction(SIGALRM, &handler, &prior) == 0;
    bq_prep_test_cancel_writer = fixture->cancel[1];
    armed = armed && setitimer(ITIMER_REAL, &timer, NULL) == 0;
    BqError cancelled = armed ? bq_retirement_check_run(&run, 6, observed) : BQ_IO;
    BQ_PREP_CHECK(armed && setitimer(ITIMER_REAL, &stopped, NULL) == 0 && sigaction(SIGALRM, &prior, NULL) == 0);
    bq_prep_test_cancel_writer = -1;
    BQ_PREP_CHECK(cancelled == BQ_WORKER_CANCEL_SIGNAL && read(fixture->cancel[0], drained, sizeof(drained)) >= 1 &&
                  bq_check_test_no_children() &&
                  fstatat(evidence, "check-receipt-0006", &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
    if (work >= 0) close(work);
    if (evidence >= 0) close(evidence);
    BQ_PREP_CHECK(bq_retirement_required_checks_release(&broken));
    BQ_PREP_CHECK(bq_check_test_install(fixture->recipes, &fixture->projection, fixture->specs, BQ_CHECK_TEST_CHECKS,
                                        fixture->profile, sizeof(fixture->profile)));
    BQ_PREP_CHECK(bq_retirement_required_checks_release(&checks));
}

/* The correctness half of the issuer. Only it sets batch_authority; it
 * refuses receipts from another job or token, a swapped binary, a missing
 * check, changed row evidence, another attempt's authority and a
 * non-native-host projection. */
BUSTER_GLOBAL_LOCAL void bq_check_test_admit(BqCheckTestFixture* fixture)
{
    BqRetirementRequiredChecks checks = {0}, other = {0};
    BqRetirementCheckResult results[BQ_CHECK_TEST_CHECKS] = {0}, foreign[BQ_CHECK_TEST_CHECKS] = {0};
    BqRetirementCheckResult swapped[BQ_CHECK_TEST_CHECKS] = {0}, missing[BQ_CHECK_TEST_CHECKS] = {0};
    BqCheckTestEvidence evidence = {0};
    BqRetirementUnitGate gate = {0};
    BqJob job = fixture->job;
    job.token += 1;
    BqRetirementProjection moved = fixture->projection;
    moved.attempt_token = job.token;
    BQ_PREP_CHECK(bq_check_test_import(fixture, &fixture->job, &fixture->projection, &checks) == BQ_OK &&
                  bq_check_test_import(fixture, &job, &moved, &other) == BQ_OK &&
                  bq_check_test_evidence(&fixture->projection, &evidence) && evidence.evidence.group_count == 1 &&
                  evidence.group.contract[0].input_count == 2);
    /* This attempt's runs, another token's runs, and runs with the held
     * binaries swapped. */
    BqRetirementHeldBinaries exchanged = fixture->held;
    exchanged.descriptors[0] = fixture->held.descriptors[1];
    exchanged.descriptors[1] = fixture->held.descriptors[0];
    BQ_PREP_CHECK(checks.owned && other.owned &&
                  bq_check_test_run_all(fixture, &checks, &fixture->held, results, NULL) &&
                  bq_check_test_run_all(fixture, &other, &fixture->held, foreign, NULL) &&
                  bq_check_test_run_all(fixture, &checks, &exchanged, swapped, NULL));
    memcpy(missing, results, sizeof(missing));
    missing[BQ_CHECK_TEST_CHECKS - 1u] = (BqRetirementCheckResult){0};
    BQ_PREP_CHECK(!strcmp(foreign[0].binary_sha256[0], results[0].binary_sha256[0]) &&
                  strcmp(foreign[0].receipt_sha256, results[0].receipt_sha256) &&
                  !strcmp(swapped[0].binary_sha256[0], results[0].binary_sha256[1]));
    BqRetirementCheckResult const* refused[] = {foreign, swapped, missing};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused); index += 1)
        BQ_PREP_CHECK(bq_retirement_unit_gate_admit(&fixture->projection, &checks, refused[index], &evidence.evidence,
                      &gate) == BQ_RECIPE_MISMATCH && !gate.owned && !gate.rows && !gate.correctness.batch_authority);
    /* Another attempt's authority, even with its own receipts. */
    BQ_PREP_CHECK(bq_retirement_unit_gate_admit(&fixture->projection, &other, foreign, &evidence.evidence, &gate) ==
                  BQ_RECIPE_MISMATCH && !gate.owned);
    /* A changed row plan field the seal covers, a changed sealed row and a
     * changed oracle: the first poisons the join, the others never enter. */
    evidence.rows[2].compiler_command_sha256[0][0] ^= 1;
    BQ_PREP_CHECK(bq_retirement_unit_gate_admit(&fixture->projection, &checks, results, &evidence.evidence, &gate) ==
                  BQ_RECIPE_MISMATCH && !gate.owned);
    evidence.rows[2].compiler_command_sha256[0][0] ^= 1;
    evidence.rows[2].configuration_sha256[0] ^= 1;
    BQ_PREP_CHECK(bq_retirement_unit_gate_admit(&fixture->projection, &checks, results, &evidence.evidence, &gate) ==
                  BQ_SOURCE_MISMATCH && !gate.owned);
    evidence.rows[2].configuration_sha256[0] ^= 1;
    evidence.rows[4].independent_oracle_sha256[0] ^= 1;
    BQ_PREP_CHECK(bq_retirement_unit_gate_admit(&fixture->projection, &checks, results, &evidence.evidence, &gate) ==
                  BQ_SOURCE_MISMATCH && !gate.owned);
    evidence.rows[4].independent_oracle_sha256[0] ^= 1;
    /* The unit times only the A1 native-host target. */
    moved = fixture->projection;
    moved.prepared.native_target = 5;
    BQ_PREP_CHECK(bq_retirement_unit_gate_admit(&moved, &checks, results, &evidence.evidence, &gate) ==
                  BQ_RECIPE_MISMATCH && !gate.owned);
    /* This attempt's passing receipts and honest rows: the finished gate
     * carries the #509 batch authority under its own seal. */
    BQ_PREP_CHECK(bq_retirement_unit_gate_admit(&fixture->projection, &checks, results, &evidence.evidence, &gate) ==
                  BQ_OK && gate.owned && gate.correctness.batch_authority == 1 &&
                  bq_retirement_correctness_ready(&gate.correctness) && gate.check_count == BQ_CHECK_TEST_CHECKS &&
                  gate.correctness.batch_groups_sha256[0] && !strcmp(gate.authority_sha256, checks.authority_sha256) &&
                  bq_retirement_hex(string_from_pointer(gate.receipts_sha256), 64) && !gate.issuer);
    /* Clearing the authority breaks the correctness seal. */
    if (gate.owned)
    {
        gate.correctness.batch_authority = 0;
        BQ_PREP_CHECK(!bq_retirement_correctness_ready(&gate.correctness));
        gate.correctness.batch_authority = 1;
        BQ_PREP_CHECK(bq_retirement_correctness_ready(&gate.correctness));
    }
    BQ_PREP_CHECK(bq_retirement_unit_gate_admit(&fixture->projection, &checks, results, &evidence.evidence, &gate) ==
                  BQ_BAD_REQUEST && gate.owned);
    BQ_PREP_CHECK(bq_retirement_unit_gate_release(&gate) && !gate.owned && !gate.rows);
    bq_check_test_evidence_release(&evidence);
    BQ_PREP_CHECK(bq_retirement_required_checks_release(&other) && bq_retirement_required_checks_release(&checks));
    BQ_PREP_CHECK(bq_check_test_no_children());
}

/* Only the step 9 issuer sets the #509 batch authority: exactly one
 * assignment in the service sources, in retirement_unit.c. */
BUSTER_GLOBAL_LOCAL void bq_check_test_single_authority_setter(void)
{
    char const* sources[] = {"tools/bench_service/retirement_unit.c", "tools/bench_service/retirement_check_runner.c",
        "tools/bench_service/retirement_correctness.c", "tools/bench_service/retirement_correctness_service.c",
        "tools/bench_service/retirement_campaign_binding.h", "tools/bench_service/retirement_campaign_service.h",
        "tools/bench_service/main.c", "tools/bench_service/worker_linux.c"};
    u32 total = 0, unit = 0;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sources); index += 1)
    {
        int file = open(sources[index], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat info = {0};
        bool ok = file >= 0 && fstat(file, &info) == 0 && info.st_size > 0 && info.st_size < 4 * 1024 * 1024;
        char* text = ok ? calloc((size_t)info.st_size + 1u, 1) : NULL;
        u32 length = 0;
        ok = ok && text && bq_read_file(file, (u8*)text, (u32)info.st_size, &length);
        for (char const* cursor = ok ? strstr(text, "batch_authority = ") : NULL; cursor;
             cursor = strstr(cursor + 1, "batch_authority = "))
        {
            total += 1;
            unit += index == 0;
        }
        BQ_PREP_CHECK(ok);
        free(text);
        if (file >= 0) close(file);
    }
    BQ_PREP_CHECK(total == 1 && unit == 1);
}

BUSTER_GLOBAL_LOCAL void bq_check_test_runner(void)
{
    u32 descriptors = bq_prep_test_open_descriptors();
    bq_check_test_single_authority_setter();
    BqCheckTestFixture* fixture = calloc(1, sizeof(*fixture));
    bool ok = fixture && bq_check_test_setup(fixture);
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        bq_check_test_importer(fixture);
        bq_check_test_runs(fixture);
        bq_check_test_admit(fixture);
    }
    if (fixture) bq_check_test_teardown(fixture);
    free(fixture);
    BQ_PREP_CHECK(bq_prep_test_open_descriptors() == descriptors && bq_check_test_no_children());
}

#endif
