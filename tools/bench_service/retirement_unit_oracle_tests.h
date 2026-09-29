/* #1020 PR 3 fixture: the whole in-unit lane-B sequence on the real A
 * fixture. Included by the preparation test runner after its helpers.
 *
 * The census is the genuine schema-2 validator fixture that
 * retirement_validator_eligibility_test.py --emit writes; its row
 * configuration digests are that module's Python reference. A dedicated
 * installed tree carries A sources whose baseline also holds the census
 * subject's bytes (the reference source), a toolchain whose bin/clang is a
 * copy of the host compiler, the census with #508's performance-row
 * population (and that module's expected_population), and a reference
 * template and inventory built for the rows the projection imports. The fixture driver
 * freezes the census manifest's literal compiler bytes, so the projection's
 * compiler/baseline join holds. The sequence is prepare, build (fixture
 * broker), project, oracle authority and reference producer; the service
 * translation unit defines BQ_RETIREMENT_REFERENCE_PRODUCER_LINKED, so forged,
 * foreign and stale tokens are exercised against the real issuer check. The
 * host compiler and fixture broker prove mechanics, not trusted Clang
 * provenance or the broker's identity split. PR 4 (bq_prep_test_unit_ready)
 * then shows the production gate refusing, writes the ready record through
 * the test-only admitted gate, replays it and tampers with each bound field,
 * the directory and the exported reference files.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_ORACLE_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_UNIT_ORACLE_TESTS_H

#define BQ_PREP_ORACLE_ROWS 192u
/* The emitted population: 192 object rows, then link@16, self-host@17
 * (native), link@64 (foreign target) and self-host@0 (untimed). */
#define BQ_PREP_ORACLE_POPULATION 196u
/* Native-runtime stage rows, hence reference rows: link@16, self-host@17. */
#define BQ_PREP_ORACLE_REFERENCES 2u
/* aarch64-unknown-linux-gnu: the fixture ledger makes every x86_64-linux row
 * unavailable, so the fixture template names another native target. */
#define BQ_PREP_ORACLE_NATIVE_TARGET 5u
#define BQ_PREP_ORACLE_UNIT_SOURCE "int unit(void) { return 1; }\n"

typedef struct BqPrepOracleFixture
{
    char installed[80], workspaces[80], queue_path[80];
    char recipes[128], census[192], sources[128];
    char toolchain_root[BQ_RETIREMENT_TOOLCHAIN_PATH_CAP];
    char template_path[256], inventory_path[256];
    char driver[160], broker[160];
    char base_profile[4096], profile[4096];
    char configurations[BQ_PREP_ORACLE_ROWS][SHA256_HEX_CAPACITY];
    /* Python expected_population, one row each: census row, stage, runtime,
     * configuration digest. */
    u32 expected_census[BQ_PREP_ORACLE_POPULATION], expected_stage[BQ_PREP_ORACLE_POPULATION];
    u32 expected_runtime[BQ_PREP_ORACLE_POPULATION];
    char expected_configuration[BQ_PREP_ORACLE_POPULATION][SHA256_HEX_CAPACITY];
    char support_sha256[SHA256_HEX_CAPACITY], rows_sha256[SHA256_HEX_CAPACITY];
    char clang_sha256[SHA256_HEX_CAPACITY], toolchain_sha256[SHA256_HEX_CAPACITY];
    char flags[5][BQ_RETIREMENT_REFERENCE_FIELD_CAP];
    BqRetirementTrustedRow rows[BQ_PREP_ORACLE_POPULATION];
    BqRetirementPreparation preparation;
    BqJob job;
    BqQueue queue;
    int installed_fd, workspaces_fd;
} BqPrepOracleFixture;

BUSTER_GLOBAL_LOCAL bool bq_prep_test_run(char* const argv[])
{
    pid_t child = fork();
    if (child == 0)
    {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    do { if (child > 0) waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    bool ok = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_file_sha(char const* path, char digest[SHA256_HEX_CAPACITY])
{
    int file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    bool ok = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_size > 0 && info.st_size < 64 * 1024 * 1024;
    u8* bytes = ok ? malloc((size_t)info.st_size) : NULL;
    u32 length = 0;
    ok = ok && bytes && bq_read_file(file, bytes, (u32)info.st_size, &length) && length == (u32)info.st_size;
    if (ok) bq_digest(bytes, length, (char8*)digest);
    free(bytes);
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* The host compiler's cc1 or libgcc directory as a -B prefix, as the
 * standalone producer fixture finds it. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_search_prefix(char const* query, char output[BQ_RETIREMENT_REFERENCE_FIELD_CAP])
{
    FILE* stream = popen(query, "r");
    char path[256] = {0};
    bool ok = stream && fgets(path, sizeof(path), stream) && path[0] == '/';
    if (stream && pclose(stream) != 0) ok = false;
    char* slash = ok ? strrchr(path, '/') : NULL;
    ok = ok && slash && (size_t)(slash - path) + 4 < BQ_RETIREMENT_REFERENCE_FIELD_CAP;
    if (ok) slash[1] = 0;
    int length = ok ? snprintf(output, BQ_RETIREMENT_REFERENCE_FIELD_CAP, "-B%s", path) : -1;
    return length > 0 && length < (int)BQ_RETIREMENT_REFERENCE_FIELD_CAP;
}

/* One installed A snapshot with src/main.c and, for the baseline, the
 * census subject's bytes at tests/unit.c. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_oracle_source(char const* installed, char const* revision,
    char const* main_contents, char const* unit_contents, BqRetirementSource* expected)
{
    char root[256], src[288], tests[288], file[320], manifest[320], text[1024];
    char8 main_digest[SHA256_HEX_CAPACITY] = {0}, unit_digest[SHA256_HEX_CAPACITY] = {0};
    int lengths[5] = {snprintf(root, sizeof(root), "%s/sources/%s", installed, revision),
                      snprintf(src, sizeof(src), "%s/src", root), snprintf(tests, sizeof(tests), "%s/tests", root),
                      snprintf(manifest, sizeof(manifest), "%s/source.manifest", root), 0};
    bool ok = true;
    for (u32 index = 0; index < 4; index += 1) ok = ok && lengths[index] > 0 && lengths[index] < 256;
    ok = ok && mkdir(root, 0700) == 0 && mkdir(src, 0700) == 0;
    lengths[4] = ok ? snprintf(file, sizeof(file), "%s/main.c", src) : -1;
    ok = ok && lengths[4] > 0 && (size_t)lengths[4] < sizeof(file) && bq_prep_test_write(file, main_contents);
    if (ok && unit_contents)
    {
        lengths[4] = snprintf(file, sizeof(file), "%s/unit.c", tests);
        ok = mkdir(tests, 0700) == 0 && lengths[4] > 0 && (size_t)lengths[4] < sizeof(file) &&
             bq_prep_test_write(file, unit_contents) && chmod(tests, 0500) == 0;
        bq_digest(unit_contents, (u32)strlen(unit_contents), unit_digest);
    }
    bq_digest(main_contents, (u32)strlen(main_contents), main_digest);
    int length = ok ? snprintf(text, sizeof(text), "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=%s\n"
                               "%.64s src/main.c\n", revision, main_digest) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(text);
    if (ok && unit_contents)
    {
        int extra = snprintf(text + length, sizeof(text) - (size_t)length, "%.64s tests/unit.c\n", unit_digest);
        ok = extra > 0 && (size_t)extra < sizeof(text) - (size_t)length;
        length += extra;
    }
    ok = ok && bq_prep_test_write(manifest, text) && chmod(src, 0500) == 0 && chmod(root, 0500) == 0;
    if (ok)
    {
        *expected = (BqRetirementSource){0};
        memcpy(expected->commit, revision, strlen(revision) + 1);
        memset(expected->tree, revision[0] == 'a' ? 'c' : 'd', 40);
        bq_digest(text, (u32)length, (char8*)expected->manifest_sha256);
        expected->entries = unit_contents ? 2 : 1;
        expected->bytes = strlen(main_contents) + (unit_contents ? strlen(unit_contents) : 0);
        expected->directories = unit_contents ? 3 : 2;
        expected->max_path = unit_contents ? 12 : 10;
        expected->max_depth = 2;
        expected->manifest_bytes = (u32)length;
    }
    return ok;
}

/* bin/clang is a copy of the host compiler, since the producer executes the
 * held Clang; the other bundle tools stay placeholders. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_oracle_toolchain(BqPrepOracleFixture* fixture)
{
    char parent[160], bin[BQ_RETIREMENT_TOOLCHAIN_PATH_CAP + 8], path[BQ_RETIREMENT_TOOLCHAIN_PATH_CAP + 32];
    char manifest[1024], compiler[4096], tool_digest[SHA256_HEX_CAPACITY] = {0};
    int lengths[3] = {snprintf(parent, sizeof(parent), "%s/toolchain", fixture->installed),
                      snprintf(fixture->toolchain_root, sizeof(fixture->toolchain_root),
                               "%s/native-retirement-performance-v1", parent),
                      snprintf(bin, sizeof(bin), "%s/bin", fixture->toolchain_root)};
    bool ok = lengths[0] > 0 && (size_t)lengths[0] < sizeof(parent) && lengths[1] > 0 &&
              (size_t)lengths[1] < sizeof(fixture->toolchain_root) && lengths[2] > 0 && (size_t)lengths[2] < sizeof(bin) &&
              mkdir(parent, 0700) == 0 && mkdir(fixture->toolchain_root, 0700) == 0 && mkdir(bin, 0700) == 0 &&
              realpath("/usr/bin/cc", compiler) != NULL;
    char const* names[] = {"clang", "cmake", "ld", "ninja"};
    u32 used = (u32)snprintf(manifest, sizeof(manifest), "BQ-RETIREMENT-TOOLCHAIN-V1\nplatform=linux-x86_64\n");
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        int length = snprintf(path, sizeof(path), "%s/%s", bin, names[index]);
        ok = length > 0 && (size_t)length < sizeof(path) &&
             (index ? bq_prep_test_write(path, "fixture only\n") : bq_prep_test_copy_file(compiler, path)) &&
             chmod(path, 0555) == 0 && bq_prep_test_file_sha(path, tool_digest);
        if (ok && !index) memcpy(fixture->clang_sha256, tool_digest, SHA256_HEX_CAPACITY);
        length = ok ? snprintf(manifest + used, sizeof(manifest) - used, "%.64s bin/%s\n", tool_digest,
                               names[index]) : -1;
        ok = ok && length > 0 && (u32)length < sizeof(manifest) - used;
        if (ok) used += (u32)length;
    }
    int length = ok ? snprintf(path, sizeof(path), "%s/toolchain.manifest", fixture->toolchain_root) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(path) && bq_prep_test_write(path, manifest) &&
         chmod(path, 0444) == 0 && chmod(bin, 0555) == 0 && chmod(fixture->toolchain_root, 0555) == 0 &&
         chmod(parent, 0555) == 0;
    if (ok) bq_digest(manifest, used, (char8*)fixture->toolchain_sha256);
    return ok;
}

/* Reference template and inventory for the derived rows: both native stage
 * rows compile the census subject from the baseline copy. hang makes the
 * second reference program spin, for the cancellation and deadline cases. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_oracle_install(BqPrepOracleFixture* fixture, bool hang)
{
    u32 const population = BQ_PREP_ORACLE_POPULATION;
    BqRetirementOracleTemplateRow approved[BQ_PREP_ORACLE_REFERENCES] = {0};
    BqRetirementReferencePlanRow plan_rows[BQ_PREP_ORACLE_REFERENCES] = {0};
    BqRetirementOracleTemplate template = {.population_rows = population, .object_rows = BQ_PREP_ORACLE_ROWS,
        .native_target = BQ_PREP_ORACLE_NATIVE_TARGET, .reference_count = BQ_PREP_ORACLE_REFERENCES,
        .references = approved};
    BqRetirementReferenceSourceIdentity source[2] = {0};
    for (u32 side = 0; side < 2; side += 1)
    {
        BqRetirementSource const* subject = fixture->preparation.subjects + side;
        memcpy(source[side].commit, subject->commit, 41);
        memcpy(source[side].tree, subject->tree, 41);
        memcpy(source[side].manifest_sha256, subject->manifest_sha256, SHA256_HEX_CAPACITY);
        memcpy(template.source_commit[side], subject->commit, 41);
        memcpy(template.source_tree[side], subject->tree, 41);
        memcpy(template.source_sha256[side], subject->manifest_sha256, SHA256_HEX_CAPACITY);
    }
    memcpy(template.support_sha256, fixture->support_sha256, SHA256_HEX_CAPACITY);
    memcpy(template.census_sha256, fixture->rows_sha256, SHA256_HEX_CAPACITY);
    memcpy(template.toolchain_identity_sha256, fixture->toolchain_sha256, SHA256_HEX_CAPACITY);
    bool ok = bq_retirement_oracle_population_hash(fixture->rows, population, template.population_sha256);
    u32 index = 0;
    for (u32 declared = 0; ok && declared < population; declared += 1)
    {
        BqRetirementTrustedRow const* row = fixture->rows + declared;
        if (!(row->compiler_eligible && row->execution_obligation && row->stage != BQ_RETIREMENT_STAGE_OBJECT &&
              row->target == BQ_PREP_ORACLE_NATIVE_TARGET)) continue;
        ok = index < BQ_PREP_ORACLE_REFERENCES;
        if (!ok) break;
        approved[index] = (BqRetirementOracleTemplateRow){.row = row->row, .census_row = row->census_row,
                                                          .target = row->target};
        memcpy(approved[index].source_sha256, row->source_sha256, SHA256_HEX_CAPACITY);
        memcpy(approved[index].configuration_sha256, row->configuration_sha256, SHA256_HEX_CAPACITY);
        snprintf(approved[index].output_name, sizeof(approved[index].output_name), "oracle-output-%u", index);
        BqRetirementReferencePlanRow* plan = plan_rows + index;
        *plan = (BqRetirementReferencePlanRow){.row = row->row, .source_side = 0, .flag_count = 4,
            .build_environment_count = 3, .runtime_argument_count = 1, .runtime_environment_count = 2};
        strcpy(plan->source_path, "tests/unit.c");
        memcpy(plan->source_sha256, row->source_sha256, SHA256_HEX_CAPACITY);
        for (u32 flag = 0; flag < 3; flag += 1) plan->flags[flag] = fixture->flags[flag];
        plan->flags[3] = hang && index == 1 ? fixture->flags[4] : fixture->flags[3];
        plan->build_environment[0] = "HOME=/nonexistent";
        plan->build_environment[1] = "LC_ALL=C";
        plan->build_environment[2] = "PATH=/usr/bin";
        plan->runtime_environment[0] = "HOME=/nonexistent";
        plan->runtime_environment[1] = "LC_ALL=C";
        ok = bq_ref_plan_row(plan, approved + index, approved[index].build_command_sha256) &&
             bq_ref_runtime_command(plan, approved[index].logical_command_sha256);
        index += 1;
    }
    ok = ok && index == BQ_PREP_ORACLE_REFERENCES;
    BqRetirementReferencePlan plan = {.template = &template, .rows = plan_rows,
                                      .count = BQ_PREP_ORACLE_REFERENCES};
    memcpy(plan.clang_sha256, fixture->clang_sha256, SHA256_HEX_CAPACITY);
    char template_sha256[SHA256_HEX_CAPACITY] = {0}, inventory_sha256[SHA256_HEX_CAPACITY] = {0};
    u8 bytes[4096];
    u64 length = 0;
    ok = ok && bq_retirement_oracle_template_hash(&template, template_sha256) &&
         bq_retirement_reference_template_write(&template, bytes, sizeof(bytes), &length) &&
         chmod(fixture->recipes, 0700) == 0 &&
         (unlink(fixture->template_path) == 0 || errno == ENOENT) &&
         (unlink(fixture->inventory_path) == 0 || errno == ENOENT) &&
         bq_prep_test_write_bytes(fixture->template_path, (char const*)bytes, (u32)length);
    int writer = ok ? open(fixture->inventory_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && writer >= 3 && bq_retirement_reference_inventory_encode(&plan, source, fixture->toolchain_sha256,
                                                                       writer, inventory_sha256) &&
         fchmod(writer, 0400) == 0;
    if (writer >= 0 && close(writer) != 0) ok = false;
    ok = ok && chmod(fixture->recipes, 0500) == 0;
    int profile = ok ? snprintf(fixture->profile, sizeof(fixture->profile),
                                "%sreference-template-sha256=%s\nreference-inventory-sha256=%s\n",
                                fixture->base_profile, template_sha256, inventory_sha256) : -1;
    return ok && profile > 0 && (size_t)profile < sizeof(fixture->profile);
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_oracle_setup(BqPrepOracleFixture* fixture)
{
    *fixture = (BqPrepOracleFixture){.installed_fd = -1, .workspaces_fd = -1,
                                     .queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1}};
    strcpy(fixture->installed, "/tmp/bq-retirement-oracle-installed-XXXXXX");
    strcpy(fixture->workspaces, "/tmp/bq-retirement-oracle-workspaces-XXXXXX");
    strcpy(fixture->queue_path, "/tmp/bq-retirement-oracle-queue-XXXXXX");
    /* An unmade directory is forgotten, so teardown removes exactly the ones
     * that exist (a random suffix may itself contain an X). */
    char* made[] = {fixture->installed, fixture->workspaces, fixture->queue_path};
    bool ok = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(made); index += 1)
    {
        if (!mkdtemp(made[index])) made[index][0] = 0;
        ok = ok && made[index][0];
    }
    int lengths[6] = {
        snprintf(fixture->recipes, sizeof(fixture->recipes), "%s/recipes", fixture->installed),
        snprintf(fixture->sources, sizeof(fixture->sources), "%s/sources", fixture->installed),
        snprintf(fixture->census, sizeof(fixture->census), "%s/" BQ_RETIREMENT_UNIT_CENSUS_DIRECTORY, fixture->recipes),
        snprintf(fixture->template_path, sizeof(fixture->template_path),
                 "%s/native-retirement-performance-v1.reference-template", fixture->recipes),
        snprintf(fixture->inventory_path, sizeof(fixture->inventory_path),
                 "%s/native-retirement-performance-v1.reference-inventory", fixture->recipes),
        snprintf(fixture->driver, sizeof(fixture->driver), "%s/fixture-driver", fixture->workspaces)};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(lengths); index += 1) ok = ok && lengths[index] > 0 && lengths[index] < 128;
    int broker_length = snprintf(fixture->broker, sizeof(fixture->broker), "%s/fixture-broker", fixture->workspaces);
    ok = ok && broker_length > 0 && (size_t)broker_length < sizeof(fixture->broker) &&
         mkdir(fixture->recipes, 0700) == 0 && mkdir(fixture->sources, 0700) == 0;
    /* The genuine schema-2 fixture, emitted by its Python reference. */
    char* emit[] = {"python3", "-W", "error", "tools/bench_service/retirement_validator_eligibility_test.py",
                    "--emit", fixture->census, NULL};
    ok = ok && bq_prep_test_run(emit);
    char path[320], text[BQ_PREP_ORACLE_ROWS * SHA256_HEX_CAPACITY + 8];
    int length = ok ? snprintf(path, sizeof(path), "%s/configurations.txt", fixture->census) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(path) &&
         bq_prep_test_read_text(path, text, sizeof(text)) == BQ_PREP_ORACLE_ROWS * SHA256_HEX_CAPACITY;
    for (u32 row = 0; ok && row < BQ_PREP_ORACLE_ROWS; row += 1)
    {
        memcpy(fixture->configurations[row], text + row * SHA256_HEX_CAPACITY, 64);
        ok = text[row * SHA256_HEX_CAPACITY + 64] == '\n';
    }
    ok = ok && unlink(path) == 0;
    char population_text[BQ_PREP_ORACLE_POPULATION * 192];
    length = ok ? snprintf(path, sizeof(path), "%s/population.txt", fixture->census) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(path) &&
         bq_prep_test_read_text(path, population_text, sizeof(population_text)) > 0;
    char* line = population_text;
    for (u32 row = 0; ok && row < BQ_PREP_ORACLE_POPULATION; row += 1)
    {
        u32 declared = 0;
        int consumed = 0;
        ok = sscanf(line, "VALIDATOR_POPULATION row=%u census=%u stage=%u runtime=%u configuration=%64s\n%n",
                    &declared, &fixture->expected_census[row], &fixture->expected_stage[row],
                    &fixture->expected_runtime[row], fixture->expected_configuration[row], &consumed) == 5 &&
             declared == row && consumed > 0;
        line += ok ? consumed : 0;
    }
    ok = ok && *line == 0 && unlink(path) == 0 && chmod(fixture->census, 0500) == 0;
    /* One pin per installed census file, in BqRetirementCensusFile order. */
    static char const* const names[] = {BQ_RETIREMENT_UNIT_CENSUS_FILES};
    static char const* const keys[] = {"support-declaration-sha256", "validator-source-applicability-sha256",
        "census-inputs-sha256", "census-rows-sha256", "census-manifest-sha256", "validator-report-sha256",
        "validator-applicability-sha256", "validator-skips-sha256", "performance-rows-sha256"};
    char pins[BUSTER_ARRAY_LENGTH(names)][SHA256_HEX_CAPACITY] = {{0}};
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        length = snprintf(path, sizeof(path), "%s/%s", fixture->census, names[index]);
        ok = length > 0 && (size_t)length < sizeof(path) && bq_prep_test_file_sha(path, pins[index]);
    }
    memcpy(fixture->support_sha256, pins[BQ_RETIREMENT_CENSUS_SUPPORT_DECLARATION], SHA256_HEX_CAPACITY);
    memcpy(fixture->rows_sha256, pins[BQ_RETIREMENT_CENSUS_ROWS], SHA256_HEX_CAPACITY);
    /* The baseline copy holds the census subject's exact bytes, the
     * reference source; both subjects carry the fixture driver's marker. */
    char const* base = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char const* candidate = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    BqRetirementSource subjects[2] = {0};
    ok = ok && bq_prep_test_oracle_source(fixture->installed, base, "/* census-fixture baseline */\n",
                                          BQ_PREP_ORACLE_UNIT_SOURCE, &subjects[0]) &&
         bq_prep_test_oracle_source(fixture->installed, candidate, "/* census-fixture candidate */\n", NULL,
                                    &subjects[1]) &&
         bq_prep_test_oracle_toolchain(fixture);
    char inventory[1024], inventory_sha256[SHA256_HEX_CAPACITY] = {0};
    length = ok ? snprintf(inventory, sizeof(inventory),
        "BQ-RETIREMENT-INPUTS-V1\nrepository=buster14a/buster\nsupport-sha256=%.64s\ncontract-sha256=%.64s\n"
        "base=%s %s %s %u %" PRIu64 " %u %u %u\ncandidate=%s %s %s %u %" PRIu64 " %u %u %u\n",
        fixture->support_sha256, "2222222222222222222222222222222222222222222222222222222222222222",
        subjects[0].commit, subjects[0].tree, subjects[0].manifest_sha256, subjects[0].entries,
        (uint64_t)subjects[0].bytes, subjects[0].directories, subjects[0].max_path, subjects[0].max_depth,
        subjects[1].commit, subjects[1].tree, subjects[1].manifest_sha256, subjects[1].entries,
        (uint64_t)subjects[1].bytes, subjects[1].directories, subjects[1].max_path, subjects[1].max_depth) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(inventory);
    if (ok) bq_digest(inventory, (u32)length, (char8*)inventory_sha256);
    int inventory_path = ok ? snprintf(path, sizeof(path), "%s/native-retirement-performance-v1.inventory",
                                       fixture->recipes) : -1;
    ok = ok && inventory_path > 0 && (size_t)inventory_path < sizeof(path) && bq_prep_test_write(path, inventory);
    char driver_sha256[SHA256_HEX_CAPACITY] = {0};
    ok = ok && bq_prep_test_compile_driver(fixture->driver) &&
         bq_prep_test_copy_file(fixture->driver, fixture->broker) &&
         bq_retirement_build_driver_sha(fixture->driver, driver_sha256);
    length = ok ? snprintf(fixture->base_profile, sizeof(fixture->base_profile),
        "schema=1\nrecipe=native-retirement-performance-v1\ncontract-sha256=%.64s\ninventory-sha256=%s\n"
        "toolchain-manifest-sha256=%s\nbuild-driver-sha256=%s\n",
        "2222222222222222222222222222222222222222222222222222222222222222", inventory_sha256,
        fixture->toolchain_sha256, driver_sha256) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(fixture->base_profile);
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(keys); index += 1)
    {
        int extra = snprintf(fixture->base_profile + length, sizeof(fixture->base_profile) - (size_t)length,
                             "%s=%s\n", keys[index], pins[index]);
        ok = extra > 0 && (size_t)extra < sizeof(fixture->base_profile) - (size_t)length;
        length += extra;
    }
    ok = ok && chmod(fixture->recipes, 0500) == 0 && chmod(fixture->sources, 0500) == 0 &&
         chmod(fixture->installed, 0555) == 0 && chmod(fixture->workspaces, 02710) == 0;
    fixture->installed_fd = ok ? open(fixture->installed, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    fixture->workspaces_fd = ok ? open(fixture->workspaces, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && fixture->installed_fd >= 3 && fixture->workspaces_fd >= 3;
    /* The template must carry the population the projection imports, so
     * import it here through the same functions with the pinned files. */
    BqRetirementCensusFiles census;
    for (u32 index = 0; index < BQ_RETIREMENT_CENSUS_FILE_COUNT; index += 1) census.descriptors[index] = -1;
    BqRetirementValidatorEligibility eligibility = {0};
    String8 base_profile = string_from_pointer(fixture->base_profile);
    u8* population = NULL;
    u64 population_length = 0;
    char population_sha256[SHA256_HEX_CAPACITY] = {0};
    BqRetirementTrustedRow* imported = NULL;
    u32 imported_rows = 0;
    ok = ok && bq_retirement_unit_census_open(fixture->installed_fd, &census) == BQ_OK &&
         bq_retirement_validator_eligibility_projection(census.descriptors[0], census.descriptors[1],
             census.descriptors[2], census.descriptors[3], census.descriptors[4], census.descriptors[5],
             census.descriptors[6], census.descriptors[7], base_profile, true, &eligibility) &&
         eligibility.row_count == BQ_PREP_ORACLE_ROWS &&
         bq_retirement_validator_read_pinned(census.descriptors[BQ_RETIREMENT_CENSUS_PERFORMANCE_ROWS], base_profile,
             S8("performance-rows-sha256="), BQ_RETIREMENT_POPULATION_BYTES_CAP, &population, &population_length,
             population_sha256) &&
         bq_retirement_performance_rows_derive((String8){(char8*)population, population_length}, base_profile,
             &eligibility, BQ_PREP_ORACLE_NATIVE_TARGET, &imported, &imported_rows) &&
         imported_rows == BQ_PREP_ORACLE_POPULATION;
    if (ok) memcpy(fixture->rows, imported, sizeof(fixture->rows));
    free(imported);
    free(population);
    bq_retirement_validator_eligibility_release(&eligibility);
    if (!bq_retirement_unit_census_close(&census)) ok = false;
    /* Commands and flags for both reference rows. */
    strcpy(fixture->flags[0], "-std=c11");
    strcpy(fixture->flags[3], "-Dunit(v)=puts(const char*); int main(v) { return puts(\"independent\") < 0; } "
                              "int unused(v)");
    strcpy(fixture->flags[4], "-Dunit(v)=main(v) { for (;;) {} } int unused(v)");
    ok = ok && bq_prep_test_search_prefix("/usr/bin/cc -print-prog-name=cc1", fixture->flags[1]) &&
         bq_prep_test_search_prefix("/usr/bin/cc -print-libgcc-file-name", fixture->flags[2]);
    String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("oracle"), S8("native-retirement-performance-v1"),
                                      string_from_pointer(base), string_from_pointer(candidate)};
    fixture->job = (BqJob){0};
    for (u32 index = 0; ok && index < BQ_FIELD_COUNT; index += 1)
    {
        bq_put32(fixture->job.request.bytes + fixture->job.request.size, (u32)fields[index].length);
        fixture->job.request.size += 4;
        memcpy(fixture->job.request.bytes + fixture->job.request.size, fields[index].pointer,
               (size_t)fields[index].length);
        fixture->job.request.size += (u32)fields[index].length;
    }
    if (ok) bq_request_digest(&fixture->job.request, fixture->job.digest);
    ok = ok && bq_retirement_preflight_pinned(fixture->installed_fd, fixture->workspaces_fd, &fixture->job.request,
                                              base_profile, &fixture->preparation) == BQ_OK &&
         bq_prep_test_oracle_install(fixture, false) && bq_open(&fixture->queue, fixture->queue_path) == BQ_OK;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_oracle_teardown(BqPrepOracleFixture* fixture)
{
    if (fixture->queue.directory_fd >= 0) bq_close(&fixture->queue);
    if (fixture->installed_fd >= 0) close(fixture->installed_fd);
    if (fixture->workspaces_fd >= 0) close(fixture->workspaces_fd);
    if (fixture->queue_path[0]) bq_prep_test_cleanup(fixture->queue_path);
    if (fixture->workspaces[0]) bq_prep_test_cleanup(fixture->workspaces);
    if (fixture->installed[0]) bq_prep_test_cleanup(fixture->installed);
}

/* Live (unreaped or running) children of this process, through /proc. */
BUSTER_GLOBAL_LOCAL u32 bq_prep_test_live_children(void)
{
    DIR* listing = opendir("/proc");
    u32 count = 0;
    for (struct dirent* entry = listing ? readdir(listing) : NULL; entry; entry = readdir(listing))
    {
        char path[288], text[512];
        if (entry->d_name[0] < '1' || entry->d_name[0] > '9') continue;
        int length = snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);
        char* state = length > 0 && (size_t)length < sizeof(path) && bq_prep_test_read_text(path, text, sizeof(text)) ?
                      strrchr(text, ')') : NULL;
        long parent = state && state[1] == ' ' && state[2] ? strtol(state + 4, NULL, 10) : 0;
        if (parent == (long)getpid()) count += 1;
    }
    if (listing) closedir(listing);
    return count;
}

typedef struct BqPrepOracleAttempt
{
    BqPrepUnitAttempt attempt;
    BqRetirementUnitBuilt built;
    BqRetirementProjection projection;
} BqPrepOracleAttempt;

/* A fresh attempt through prepare, the fixture broker build and the unit's
 * census projection, with the fixture's current profile. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_oracle_attempt(BqPrepOracleFixture* fixture, u64 id, int cancellation_fd,
    BqPrepOracleAttempt* out)
{
    *out = (BqPrepOracleAttempt){.built = {.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}}};
    String8 profile = string_from_pointer(fixture->profile);
    String8 root = string_from_pointer(fixture->workspaces);
    bool ok = bq_prep_test_unit_attempt(&fixture->queue, &fixture->job, id, fixture->installed_fd,
                                        fixture->workspaces_fd, &fixture->preparation, fixture->profile,
                                        fixture->toolchain_root, &out->attempt);
    BqPhaseChannel phases = {.descriptor = -1};
    pid_t peer = ok ? bq_prep_test_phase_peer(&phases, id, id + 10u, true) : -1;
    ok = ok && bq_retirement_unit_build_pinned(out->attempt.store, &out->attempt.unit, fixture->workspaces_fd,
                   fixture->installed_fd, root, profile, fixture->driver, fixture->toolchain_root, fixture->broker,
                   fixture->workspaces, geteuid(), &phases, cancellation_fd, bq_phase_clock() + 300000000000ull,
                   &out->built) == BQ_OK;
    if (peer > 0) ok = bq_prep_test_phase_peer_join(&phases, peer) && ok;
    ok = ok && bq_retirement_unit_project_pinned(out->attempt.store, &out->attempt.unit, &out->built,
                   fixture->workspaces_fd, fixture->installed_fd, root, profile, S8("self-test"), fixture->driver,
                   fixture->toolchain_root, fixture->broker, fixture->workspaces, &out->projection) == BQ_OK;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_oracle_attempt_close(BqPrepOracleAttempt* attempt)
{
    bool ok = bq_retirement_projection_release(&attempt->projection);
    ok = bq_retirement_unit_built_release(&attempt->built) && ok;
    return bq_prep_test_unit_attempt_close(&attempt->attempt) && ok;
}

/* A hand-driven authority and producer over a copy of an attempt's derived
 * rows, for the token boundary. */
typedef struct BqPrepOraclePair
{
    BqRetirementOracleAuthority authority;
    BqRetirementOracleReference references[BQ_PREP_ORACLE_REFERENCES];
    BqRetirementReferenceSourceFile sources[BQ_PREP_ORACLE_REFERENCES];
    BqRetirementReferenceProducer producer;
    BqRetirementTrustedRow* rows;
    int roots[2], output;
} BqPrepOraclePair;

BUSTER_GLOBAL_LOCAL bool bq_prep_test_oracle_pair(BqPrepOracleFixture* fixture, BqPrepOracleAttempt const* attempt,
    char const* name, BqPrepOraclePair* pair)
{
    *pair = (BqPrepOraclePair){.producer = {.source_file = -1, .binary_file = -1, .receipt_file = -1},
                               .roots = {-1, -1}, .output = -1};
    BqRetirementUnitPrepared const* unit = &attempt->attempt.unit;
    u32 count = attempt->projection.prepared.rows;
    pair->rows = malloc((size_t)count * sizeof(*pair->rows));
    bool ok = pair->rows != NULL;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        pair->rows[index] = attempt->projection.rows[index];
        memset(pair->rows[index].independent_oracle_sha256, 0, SHA256_HEX_CAPACITY);
    }
    ok = ok && bq_retirement_oracle_authority_begin(&pair->authority, &unit->policy.template,
                   unit->policy.template_sha256, &attempt->projection.prepared, pair->rows, pair->references,
                   BQ_PREP_ORACLE_REFERENCES, unit->job.id, unit->job.token) &&
         mkdirat(fixture->workspaces_fd, name, 0700) == 0;
    pair->output = ok ? openat(fixture->workspaces_fd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    for (u32 side = 0; ok && side < 2; side += 1)
        pair->roots[side] = bq_retirement_unit_source_root(fixture->workspaces_fd, unit, side);
    ok = ok && pair->output >= 3 && pair->roots[0] >= 3 && pair->roots[1] >= 3 &&
         bq_retirement_reference_producer_begin(&pair->producer, &unit->policy.plan, unit->policy.inventory_sha256,
             unit->policy.template_sha256, unit->policy.inventory, unit->policy.source, pair->roots,
             unit->policy.toolchain_manifest_sha256, unit->policy.clang, pair->output, pair->sources,
             BQ_PREP_ORACLE_REFERENCES, &pair->authority);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_oracle_pair_close(BqPrepOraclePair* pair)
{
    bool ok = bq_retirement_reference_producer_release(&pair->producer);
    for (u32 side = 0; side < 2; side += 1)
        if (pair->roots[side] >= 0 && close(pair->roots[side]) != 0) ok = false;
    if (pair->output >= 0 && close(pair->output) != 0) ok = false;
    free(pair->rows);
    *pair = (BqPrepOraclePair){.roots = {-1, -1}, .output = -1};
    return ok;
}

/* Worker-unit B steps are compiled into the service but no production path
 * reaches them yet: bq_worker_unit and the rest of the worker name none. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_unit_oracle_unreached(void)
{
    char const* sources[] = {"tools/bench_service/worker_linux.c", "tools/bench_service/main.c",
                             "tools/bench_service/workspace.c", "tools/bench_service/queue.c"};
    char const* callers[] = {"bq_retirement_unit_", "bq_retirement_correctness_project_service",
                             "bq_retirement_correctness_begin_service", "bq_retirement_oracle_authority_",
                             "bq_retirement_reference_producer_"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sources); index += 1)
    {
        int file = open(sources[index], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat info = {0};
        bool ok = file >= 0 && fstat(file, &info) == 0 && info.st_size > 0 && info.st_size < 4 * 1024 * 1024;
        char* text = ok ? calloc((size_t)info.st_size + 1u, 1) : NULL;
        u32 length = 0;
        ok = ok && text && bq_read_file(file, (u8*)text, (u32)info.st_size, &length) && length == (u32)info.st_size;
        for (u32 caller = 0; ok && caller < BUSTER_ARRAY_LENGTH(callers); caller += 1)
            ok = strstr(text, callers[caller]) == NULL;
        /* The service unit never enables the test-only gate issuer. */
        if (index == 1) ok = ok && strstr(text, "#define BQ_RETIREMENT_CORRECTNESS_TEST_ONLY") == NULL;
        BQ_PREP_CHECK(ok);
        free(text);
        if (file >= 0) close(file);
    }
    /* The recipe gates reject the retirement job before any B step. */
    BQ_PREP_CHECK(!bq_recipe_admitted(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED) &&
                  bq_worker_unit(S8("/unsupported"), S8("1"), S8("2"), S8("native-retirement-performance-v1"),
                                 S8("/workspace"), S8("1111111111111111111111111111111111111111"),
                                 S8("2222222222222222222222222222222222222222"), S8("/workspace/result")) ==
                  BQ_BAD_REQUEST);
}

/* #1020 PR 4: the ready record and the coordinator replay on the finished
 * oracle attempt. The replay runs with the fixture's profile and broker, the
 * way the coordinator would with the compiled ones. */
#define BQ_PREP_READY_CAP 8192u

BUSTER_GLOBAL_LOCAL BqError bq_prep_test_replay(BqPrepOracleFixture* fixture, BqPrepOracleAttempt const* attempt,
    char const digest[SHA256_HEX_CAPACITY])
{
    BqError result = bq_retirement_unit_replay_pinned(attempt->attempt.store, fixture->workspaces_fd,
        fixture->installed_fd, attempt->attempt.job.id, attempt->attempt.job.token,
        string_from_pointer(fixture->workspaces), string_from_pointer(fixture->profile), S8("self-test"),
        fixture->driver, fixture->toolchain_root, fixture->broker, fixture->workspaces, attempt->attempt.digest,
        digest);
    return result;
}

/* Replace the sealed record named by current with bytes under their own
 * content address, as a writer that controls the record could; digest
 * receives that address. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_ready_install(int attempt, char digest[SHA256_HEX_CAPACITY], char const* bytes,
    u32 length)
{
    char current[80], name[80];
    int old_length = snprintf(current, sizeof(current), "ready-%s", digest);
    bq_digest(bytes, length, (char8*)digest);
    int new_length = snprintf(name, sizeof(name), "ready-%s", digest);
    int ready = openat(attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool ok = old_length > 0 && new_length > 0 && ready >= 0 && fchmod(ready, 0700) == 0 &&
              unlinkat(ready, current, 0) == 0;
    int writer = ok ? openat(ready, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400) : -1;
    ok = ok && writer >= 0 && bq_write_all(writer, (u8 const*)bytes, length);
    if (writer >= 0 && close(writer) != 0) ok = false;
    ok = ok && fchmod(ready, BQ_RETIREMENT_EXPORT_MODE) == 0;
    if (ready >= 0) close(ready);
    return ok;
}

/* Flip the first byte of the field-th space-separated field after
 * "\n<key>". */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_ready_tamper(char* bytes, char const* key, u32 field)
{
    char pattern[48];
    int length = snprintf(pattern, sizeof(pattern), "\n%s", key);
    char* cursor = length > 0 ? strstr(bytes, pattern) : NULL;
    if (cursor) cursor += length;
    for (u32 skip = 0; cursor && skip < field; skip += 1)
    {
        cursor = strchr(cursor, ' ');
        if (cursor) cursor += 1;
    }
    bool ok = cursor && *cursor && *cursor != '\n';
    if (ok) *cursor = *cursor == '0' ? '1' : '0';
    return ok;
}

/* Replace the field-th field after "\n<key>" with a decimal value. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_ready_set(char* bytes, u32* length, char const* key, u32 field, int value)
{
    char pattern[48], text[16];
    int pattern_length = snprintf(pattern, sizeof(pattern), "\n%s", key);
    int text_length = snprintf(text, sizeof(text), "%d", value);
    char* cursor = pattern_length > 0 ? strstr(bytes, pattern) : NULL;
    if (cursor) cursor += pattern_length;
    for (u32 skip = 0; cursor && skip < field; skip += 1)
    {
        cursor = strchr(cursor, ' ');
        if (cursor) cursor += 1;
    }
    size_t old = cursor ? strcspn(cursor, " \n") : 0;
    bool ok = cursor && old > 0 && text_length > 0 && *length - old + (u32)text_length < BQ_PREP_READY_CAP;
    if (ok)
    {
        size_t tail = strlen(cursor + old) + 1u;
        memmove(cursor + text_length, cursor + old, tail);
        memcpy(cursor, text, (size_t)text_length);
        *length = *length - (u32)old + (u32)text_length;
    }
    return ok;
}

typedef struct BqPrepReadyTamper
{
    char const* key;
    u32 field;
    BqError expected;
} BqPrepReadyTamper;

/* Replace one reference-oracle/ file with bytes (NULL removes it). */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_reference_replace(int reference, char const* name, char const* bytes,
    u32 length, mode_t mode)
{
    bool ok = fchmod(reference, 0700) == 0 && (unlinkat(reference, name, 0) == 0 || errno == ENOENT);
    int writer = ok && bytes ? openat(reference, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    if (bytes) ok = ok && writer >= 0 && bq_write_all(writer, (u8 const*)bytes, length) && fchmod(writer, mode) == 0;
    if (writer >= 0 && close(writer) != 0) ok = false;
    return fchmod(reference, BQ_RETIREMENT_EXPORT_MODE) == 0 && ok;
}

BUSTER_GLOBAL_LOCAL u32 bq_prep_test_read_at(int directory, char const* name, char* bytes, u32 capacity)
{
    int file = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t count = file >= 0 ? read(file, bytes, capacity - 1u) : -1;
    if (file >= 0) close(file);
    u32 used = count > 0 ? (u32)count : 0;
    bytes[used] = 0;
    return used;
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_unit_ready(BqPrepOracleFixture* fixture, BqPrepOracleAttempt* success,
    BqRetirementUnitOracle* oracle)
{
    u32 descriptors = bq_prep_test_open_descriptors();
    BqRetirementUnitPrepared* unit = &success->attempt.unit;
    BqRetirementProjection* projection = &success->projection;
    int attempt = success->attempt.attempt, workspaces = fixture->workspaces_fd;
    struct stat info = {0};
    char digest[SHA256_HEX_CAPACITY] = {0}, refused[SHA256_HEX_CAPACITY] = {0};
    char const* reference_path = BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/" BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY;

    /* Design step 9 in production: begin_service fails closed, so the gate
     * stays unadmitted and step 10 writes nothing and seals nothing. */
    BqRetirementUnitGate gate = {0};
    BQ_PREP_CHECK(bq_retirement_unit_gate(projection, oracle, &gate) == BQ_RECIPE_MISMATCH && !gate.issuer &&
                  !gate.seal_sha256[0]);
    BqRetirementUnitGate forged = {.issuer = 1};
    memset(forged.seal_sha256, 'a', 64);
    BqRetirementUnitGate const* unadmitted[] = {&gate, &forged, NULL};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(unadmitted); index += 1)
        BQ_PREP_CHECK(bq_retirement_unit_ready(unit, &success->built, projection, oracle, unadmitted[index],
                      workspaces, refused) == BQ_RECIPE_MISMATCH && !refused[0] &&
                      fstatat(attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                      errno == ENOENT && fstatat(attempt, reference_path, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                      (info.st_mode & 0777) == 0700);

    /* The test-only admitted gate is the only way to the success path. It
     * binds this attempt's facts: a changed row or seal is refused. */
    BQ_PREP_CHECK(bq_retirement_unit_gate_fixture_admit(unit, projection, oracle, &gate) && gate.issuer);
    forged = gate;
    forged.seal_sha256[0] = forged.seal_sha256[0] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_retirement_unit_ready(unit, &success->built, projection, oracle, &forged, workspaces,
                  refused) == BQ_RECIPE_MISMATCH);
    projection->rows[7].configuration_sha256[0] ^= 1;
    BQ_PREP_CHECK(bq_retirement_unit_ready(unit, &success->built, projection, oracle, &gate, workspaces, refused) ==
                  BQ_BAD_REQUEST && fstatat(attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, &info,
                                            AT_SYMLINK_NOFOLLOW) != 0);
    projection->rows[7].configuration_sha256[0] ^= 1;

    /* A reference output changed between the oracle and the writer fails
     * the authority comparison before retirement-ready/ exists, so the
     * restored attempt can still write its record. */
    int reference = openat(attempt, reference_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    char saved[BQ_PREP_READY_CAP], changed[BQ_PREP_READY_CAP];
    u32 saved_size = reference >= 0 ? bq_prep_test_read_at(reference, "oracle-output-0", saved, sizeof(saved)) : 0;
    memcpy(changed, saved, saved_size + 1u);
    if (saved_size) changed[saved_size - 1u] ^= 1;
    BQ_PREP_CHECK(saved_size > 0 && bq_prep_test_reference_replace(reference, "oracle-output-0", changed, saved_size,
                                                                   0400));
    BQ_PREP_CHECK(bq_retirement_unit_ready(unit, &success->built, projection, oracle, &gate, workspaces, refused) ==
                  BQ_SOURCE_MISMATCH && !refused[0] &&
                  fstatat(attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                  errno == ENOENT);
    BQ_PREP_CHECK(bq_prep_test_reference_replace(reference, "oracle-output-0", saved, saved_size, 0400));
    BQ_PREP_CHECK(bq_retirement_unit_ready(unit, &success->built, projection, oracle, &gate, workspaces, digest) ==
                  BQ_OK && bq_retirement_hex(string_from_pointer(digest), 64));

    /* One sealed record under its content address; the producer's output is
     * sealed too. A second record into the attempt is refused. */
    char name[80], bytes[BQ_PREP_READY_CAP], original[BQ_PREP_READY_CAP], path[160];
    snprintf(name, sizeof(name), "ready-%s", digest);
    snprintf(path, sizeof(path), BQ_RETIREMENT_UNIT_READY_DIRECTORY "/%s", name);
    int ready = openat(attempt, BQ_RETIREMENT_UNIT_READY_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    u32 length = ready >= 0 ? bq_prep_test_read_at(ready, name, original, sizeof(original)) : 0;
    char computed[SHA256_HEX_CAPACITY] = {0};
    bq_digest(original, length, (char8*)computed);
    BQ_PREP_CHECK(length > 0 && !strcmp(computed, digest) && !strncmp(original, "BQ-RETIREMENT-READY-V1\n", 23) &&
                  strstr(original, "\nobserved-rows=2\n") && strstr(original, "\ngate=admitted ") &&
                  fstat(ready, &info) == 0 && (info.st_mode & 07777) == BQ_RETIREMENT_EXPORT_MODE &&
                  fstatat(attempt, path, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) &&
                  (info.st_mode & 07777) == 0400 && info.st_nlink == 1 &&
                  fstatat(attempt, reference_path, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                  (info.st_mode & 07777) == BQ_RETIREMENT_EXPORT_MODE);
    BQ_PREP_CHECK(bq_retirement_unit_ready(unit, &success->built, projection, oracle, &gate, workspaces, refused) ==
                  BQ_WORKSPACE_MISMATCH && !refused[0]);

    /* The coordinator's replay re-derives the same record. The blocked
     * profile, another token, a wrong A digest and a wrong record digest
     * fail closed, and nothing leaks. */
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == BQ_OK);
    BQ_PREP_CHECK(bq_retirement_unit_replay(success->attempt.store, workspaces, fixture->installed_fd,
                  success->attempt.job.id, success->attempt.job.token, success->attempt.digest, digest) ==
                  BQ_RECIPE_MISMATCH);
    BQ_PREP_CHECK(bq_retirement_unit_replay_pinned(success->attempt.store, workspaces, fixture->installed_fd,
                  success->attempt.job.id, success->attempt.job.token + 1u, string_from_pointer(fixture->workspaces),
                  string_from_pointer(fixture->profile), S8("self-test"), fixture->driver, fixture->toolchain_root,
                  fixture->broker, fixture->workspaces, success->attempt.digest, digest) != BQ_OK);
    char wrong[SHA256_HEX_CAPACITY];
    memcpy(wrong, success->attempt.digest, sizeof(wrong));
    wrong[0] = wrong[0] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_retirement_unit_replay_pinned(success->attempt.store, workspaces, fixture->installed_fd,
                  success->attempt.job.id, success->attempt.job.token, string_from_pointer(fixture->workspaces),
                  string_from_pointer(fixture->profile), S8("self-test"), fixture->driver, fixture->toolchain_root,
                  fixture->broker, fixture->workspaces, wrong, digest) != BQ_OK);
    memcpy(wrong, digest, sizeof(wrong));
    wrong[0] = wrong[0] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, wrong) == BQ_WORKSPACE_MISMATCH);

    /* Every bound field, tampered and re-addressed so that only the replay's
     * own recomputation can catch it, each with the error of the check that
     * must catch it; the original is restored and replayed after each. A
     * header field or row value the replay recomputes fails the final
     * comparison (BQ_CORRUPT); a build digest fails its re-import; a
     * descriptor number or command digest fails the rebuilt command
     * (BQ_SOURCE_MISMATCH); the gate seal fails its verifier. */
    static BqPrepReadyTamper const tampers[] = {
        {"job=", 0, BQ_CORRUPT}, {"attempt=", 0, BQ_CORRUPT}, {"attempt-identity=", 0, BQ_CORRUPT},
        {"request=", 0, BQ_CORRUPT}, {"preparation=", 0, BQ_CORRUPT}, {"binaries=", 0, BQ_CORRUPT},
        {"matched-builds=", 0, BQ_CORRUPT}, {"binary-base=", 0, BQ_CORRUPT}, {"binary-candidate=", 0, BQ_CORRUPT},
        {"support=", 0, BQ_CORRUPT}, {"census=", 0, BQ_CORRUPT}, {"population=", 0, BQ_CORRUPT},
        {"rows=", 0, BQ_CORRUPT}, {"object-rows=", 0, BQ_CORRUPT}, {"native-target=", 0, BQ_CORRUPT},
        {"evidence=", 0, BQ_CORRUPT}, {"template=", 0, BQ_CORRUPT}, {"inventory=", 0, BQ_CORRUPT},
        {"oracle-attempt=", 0, BQ_CORRUPT}, {"oracle-observed=", 0, BQ_CORRUPT}, {"observed-rows=", 0, BQ_CORRUPT},
        {"observed=", 0, BQ_CORRUPT}, {"observed=", 1, BQ_CORRUPT}, {"observed=", 2, BQ_CORRUPT},
        {"observed=", 3, BQ_CORRUPT}, {"observed=", 4, BQ_SOURCE_MISMATCH}, {"observed=", 5, BQ_SOURCE_MISMATCH},
        {"observed=", 6, BQ_CORRUPT}, {"observed=", 7, BQ_CORRUPT}, {"observed=", 8, BQ_CORRUPT},
        {"observed=", 9, BQ_SOURCE_MISMATCH}, {"observed=", 10, BQ_CORRUPT}, {"gate=", 0, BQ_CORRUPT},
        {"gate=", 1, BQ_RECIPE_MISMATCH},
        /* Special cases: key NULL, field selects it. */
        {NULL, 0, BQ_CORRUPT}, {NULL, 1, BQ_CORRUPT}, {NULL, 2, BQ_CORRUPT}, {NULL, 3, BQ_SOURCE_MISMATCH},
        {NULL, 4, BQ_SOURCE_MISMATCH}};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(tampers); index += 1)
    {
        BqPrepReadyTamper const* tamper = tampers + index;
        memcpy(bytes, original, length + 1u);
        u32 changed_length = length;
        bool tampered = true;
        if (tamper->key) tampered = bq_prep_test_ready_tamper(bytes, tamper->key, tamper->field);
        else if (tamper->field == 0)
        {
            /* The two observed rows, reordered. */
            char* first = strstr(bytes, "\nobserved=0 ") + 1;
            char* second = strstr(bytes, "\nobserved=1 ") + 1;
            char* end = strchr(second, '\n') + 1;
            char swapped[BQ_PREP_READY_CAP];
            size_t head = (size_t)(second - first), tail = (size_t)(end - second);
            memcpy(swapped, second, tail);
            memcpy(swapped + tail, first, head);
            memcpy(first, swapped, head + tail);
        }
        else if (tamper->field == 1)
        {
            memcpy(bytes + length, "extra=1\n", 9);
            changed_length = length + 8u;
        }
        else if (tamper->field == 2) bytes[0] = 'X';
        else
        {
            /* Row 0's binary or working-directory descriptor moved to
             * another valid canonical number that is not the other one. */
            int number[2] = {oracle->descriptors[0], oracle->descriptors[1]};
            u32 moved = tamper->field - 3u;
            int value = number[moved] + 1 == number[1u - moved] ? number[moved] + 2 : number[moved] + 1;
            tampered = bq_prep_test_ready_set(bytes, &changed_length, "observed=", 4u + moved, value);
        }
        BQ_PREP_CHECK(tampered && bq_prep_test_ready_install(attempt, digest, bytes, changed_length));
        BqError result = bq_prep_test_replay(fixture, success, digest);
        if (result != tamper->expected)
            fprintf(stderr, "RETIREMENT_PREP ready tamper case %u returned %d\n", index, (int)result);
        BQ_PREP_CHECK(result == tamper->expected);
        BQ_PREP_CHECK(bq_prep_test_ready_install(attempt, digest, original, length) &&
                      bq_prep_test_replay(fixture, success, digest) == BQ_OK);
    }

    /* A consistent forgery: row 0's binary descriptor moved, and its
     * command, the observation chain, the oracle attempt and the fixture gate
     * seal all recomputed with the unit's own functions. The test build's
     * replay accepts it: only the authenticated record digest and the future
     * #509 gate anchor the record. */
    int forged_descriptors[2u * BQ_PREP_ORACLE_REFERENCES];
    memcpy(forged_descriptors, oracle->descriptors, sizeof(forged_descriptors));
    forged_descriptors[0] += forged_descriptors[0] + 1 == forged_descriptors[1] ? 2 : 1;
    BqRetirementOracleReference forged_references[BQ_PREP_ORACLE_REFERENCES];
    char forged_outputs[BQ_PREP_ORACLE_REFERENCES][SHA256_HEX_CAPACITY];
    BqRetirementProjection copy = *projection;
    copy.rows = malloc((size_t)projection->prepared.rows * sizeof(*copy.rows));
    for (u32 index = 0; copy.rows && index < projection->prepared.rows; index += 1)
    {
        copy.rows[index] = projection->rows[index];
        memset(copy.rows[index].independent_oracle_sha256, 0, SHA256_HEX_CAPACITY);
    }
    BqRetirementOracleAuthority forged_authority = {0};
    BqRetirementUnitReadyFacts facts = {.job = &unit->job, .projection = &copy, .authority = &forged_authority,
        .descriptors = forged_descriptors, .binary_record_sha256 = success->built.binary_record_sha256,
        .build_record_sha256 = success->built.build_record_sha256,
        .template_sha256 = unit->policy.template_sha256, .inventory_sha256 = unit->policy.inventory_sha256};
    u32 forged_length = 0;
    BQ_PREP_CHECK(copy.rows && bq_retirement_unit_reference_observe(reference, unit, forged_descriptors,
                      forged_references, forged_outputs) &&
                  bq_retirement_unit_replay_authority(unit, &copy, forged_references, forged_outputs,
                                                      &forged_authority) &&
                  strcmp(forged_authority.attempt_sha256, oracle->attempt_sha256) &&
                  bq_retirement_unit_attempt_sha(&unit->job, facts.attempt_sha256) &&
                  bq_retirement_unit_gate_fixture_seal(&facts, facts.gate_sha256) &&
                  bq_retirement_unit_ready_format(&facts, bytes, sizeof(bytes), &forged_length) &&
                  bq_prep_test_ready_install(attempt, digest, bytes, forged_length) &&
                  bq_prep_test_replay(fixture, success, digest) == BQ_OK);
    free(copy.rows);
    BQ_PREP_CHECK(bq_prep_test_ready_install(attempt, digest, original, length) &&
                  bq_prep_test_replay(fixture, success, digest) == BQ_OK);

    /* A crash between the temporary and the link leaves only the temporary
     * in an unsealed directory; one after the link, two links. Both fail,
     * and the unit refuses to write into the leftover directory. */
    char partial[48];
    BQ_PREP_CHECK(bq_record_name(partial, "ready-partial", success->attempt.job.id));
    BQ_PREP_CHECK(ready >= 0 && fchmod(ready, 0700) == 0 && renameat(ready, name, ready, partial) == 0);
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == BQ_WORKSPACE_MISMATCH);
    BQ_PREP_CHECK(bq_retirement_unit_ready(unit, &success->built, projection, oracle, &gate, workspaces, refused) ==
                  BQ_WORKSPACE_MISMATCH);
    BQ_PREP_CHECK(linkat(ready, partial, ready, name, 0) == 0);
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == BQ_WORKSPACE_MISMATCH);
    BQ_PREP_CHECK(unlinkat(ready, partial, 0) == 0);
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == BQ_WORKSPACE_MISMATCH);
    BQ_PREP_CHECK(fchmod(ready, BQ_RETIREMENT_EXPORT_MODE) == 0 && bq_prep_test_replay(fixture, success, digest) == BQ_OK);

    /* An extra entry, a symlinked record and a writable record. */
    BQ_PREP_CHECK(fchmod(ready, 0700) == 0 && symlinkat(name, ready, "planted") == 0 &&
                  fchmod(ready, BQ_RETIREMENT_EXPORT_MODE) == 0);
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == BQ_WORKSPACE_MISMATCH);
    BQ_PREP_CHECK(fchmod(ready, 0700) == 0 && unlinkat(ready, "planted", 0) == 0 &&
                  renameat(ready, name, attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/ready-copy") == 0 &&
                  symlinkat("../" BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/ready-copy", ready, name) == 0 &&
                  fchmod(ready, BQ_RETIREMENT_EXPORT_MODE) == 0);
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == BQ_WORKSPACE_MISMATCH);
    BQ_PREP_CHECK(fchmod(ready, 0700) == 0 && unlinkat(ready, name, 0) == 0 &&
                  renameat(attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/ready-copy", ready, name) == 0 &&
                  fchmodat(ready, name, 0600, 0) == 0 && fchmod(ready, BQ_RETIREMENT_EXPORT_MODE) == 0);
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == BQ_WORKSPACE_MISMATCH);
    BQ_PREP_CHECK(fchmod(ready, 0700) == 0 && fchmodat(ready, name, 0400, 0) == 0 &&
                  fchmod(ready, BQ_RETIREMENT_EXPORT_MODE) == 0 && bq_prep_test_replay(fixture, success, digest) == BQ_OK);

    /* The exported artifacts: an unsealed or extended reference directory,
     * a missing output, a changed output, receipt or log. */
    BQ_PREP_CHECK(reference >= 0 && fchmod(reference, 0700) == 0 && bq_prep_test_replay(fixture, success, digest) ==
                  BQ_SOURCE_MISMATCH && fchmod(reference, BQ_RETIREMENT_EXPORT_MODE) == 0);
    BQ_PREP_CHECK(bq_prep_test_reference_replace(reference, "planted", "x", 1, 0400) &&
                  bq_prep_test_replay(fixture, success, digest) == BQ_SOURCE_MISMATCH &&
                  bq_prep_test_reference_replace(reference, "planted", NULL, 0, 0));
    /* A missing file breaks the closure, a changed receipt or log its
     * rebuilt receipt; a changed output rebuilds another oracle attempt,
     * which the gate seal no longer admits. */
    char const* artifacts[] = {"oracle-output-1", "oracle-output-0", "reference-receipt-00000001",
                               "reference-log-00000000"};
    BqError const expected[] = {BQ_SOURCE_MISMATCH, BQ_RECIPE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(artifacts); index += 1)
    {
        u32 size = reference >= 0 ? bq_prep_test_read_at(reference, artifacts[index], saved, sizeof(saved)) : 0;
        /* A compiler log may be empty: then the change is one added byte. */
        memcpy(changed, size ? saved : "x", size ? size + 1u : 2u);
        if (size) changed[size - 1u] ^= 1;
        BQ_PREP_CHECK(bq_prep_test_reference_replace(reference, artifacts[index], index ? changed : NULL,
                                                     size ? size : 1u, 0400));
        BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == expected[index]);
        BQ_PREP_CHECK(bq_prep_test_reference_replace(reference, artifacts[index], saved, size, 0400) &&
                      bq_prep_test_replay(fixture, success, digest) == BQ_OK);
    }
    BQ_PREP_CHECK(bq_prep_test_replay(fixture, success, digest) == BQ_OK);
    if (reference >= 0) close(reference);
    if (ready >= 0) close(ready);
    BQ_PREP_CHECK(bq_prep_test_open_descriptors() == descriptors && bq_prep_test_live_children() == 0);
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_unit_oracle(void)
{
    bq_prep_test_unit_oracle_unreached();
    u32 descriptors = bq_prep_test_open_descriptors();
    BqPrepOracleFixture* fixture = calloc(1, sizeof(*fixture));
    bool ok = fixture && bq_prep_test_oracle_setup(fixture);
    BQ_PREP_CHECK(ok);
    int cancel[2] = {-1, -1};
    BQ_PREP_CHECK(pipe2(cancel, O_CLOEXEC | O_NONBLOCK) == 0);
    u64 generous = bq_phase_clock() + 300ull * 1000000000ull;
    String8 profile = ok ? string_from_pointer(fixture->profile) : (String8){0};
    String8 root = ok ? string_from_pointer(fixture->workspaces) : (String8){0};

    /* Prepare, build, then the projection: rows come from #508's pinned
     * population joined to the pinned census, never from a caller, and equal
     * the Python reference (census row, stage, runtime, configuration). */
    BqPrepOracleAttempt* success = calloc(1, sizeof(*success));
    ok = ok && success && bq_prep_test_oracle_attempt(fixture, 81, cancel[0], success);
    BQ_PREP_CHECK(ok);
    BqRetirementProjection* projection = success ? &success->projection : NULL;
    u32 const population = BQ_PREP_ORACLE_POPULATION;
    BQ_PREP_CHECK(ok && projection->owned && projection->prepared.rows == population &&
                  projection->prepared.object_rows == BQ_PREP_ORACLE_ROWS &&
                  projection->prepared.native_target == BQ_PREP_ORACLE_NATIVE_TARGET &&
                  !strcmp(projection->prepared.preparation_sha256, success->attempt.digest) &&
                  !strcmp(projection->prepared.binary_sha256[0], success->built.binaries.verified.binary_sha256[0]) &&
                  !memcmp(projection->rows, fixture->rows, sizeof(fixture->rows)));
    for (u32 index = 0; ok && index < population; index += 1)
    {
        BqRetirementTrustedRow const* row = projection->rows + index;
        bool runtime = row->compiler_eligible && row->execution_obligation &&
                       row->stage != BQ_RETIREMENT_STAGE_OBJECT && row->target == BQ_PREP_ORACLE_NATIVE_TARGET;
        BQ_PREP_CHECK(row->row == index && row->census_row == fixture->expected_census[index] &&
                      row->stage == fixture->expected_stage[index] && runtime == (fixture->expected_runtime[index] != 0) &&
                      !strcmp(row->configuration_sha256, fixture->expected_configuration[index]) &&
                      !strcmp(row->configuration_sha256, fixture->configurations[row->census_row]) &&
                      (index >= BQ_PREP_ORACLE_ROWS || row->census_row == index));
    }
    /* Each stage row names the census row its declaration carries. */
    BQ_PREP_CHECK(ok && projection->rows[192].census_row == 16 && projection->rows[193].census_row == 17 &&
                  projection->rows[194].census_row == 64 && projection->rows[195].census_row == 0 &&
                  projection->rows[192].stage == BQ_RETIREMENT_STAGE_LINK &&
                  projection->rows[195].stage == BQ_RETIREMENT_STAGE_SELF_HOST &&
                  strcmp(projection->rows[192].identity_sha256, projection->rows[16].identity_sha256) &&
                  strcmp(projection->rows[192].identity_sha256, projection->rows[194].identity_sha256) &&
                  projection->rows[194].compiler_eligible && projection->rows[194].target != BQ_PREP_ORACLE_NATIVE_TARGET &&
                  !projection->rows[195].compiler_eligible && projection->rows[195].skip_proof_sha256[0] &&
                  !strcmp(projection->rows[195].skip_proof_sha256, projection->rows[0].skip_proof_sha256) &&
                  !projection->rows[0].compiler_eligible && !projection->rows[0].independent_oracle_sha256[0]);

    BqRetirementProjection refused = {0};
    BqRetirementStore store = ok ? success->attempt.store : (BqRetirementStore){-1};
    BqRetirementUnitPrepared* unit = ok ? &success->attempt.unit : NULL;
    int workspaces = ok ? fixture->workspaces_fd : -1, installed = ok ? fixture->installed_fd : -1;
    if (ok)
    {
        /* A live projection, the blocked profile, a full-census requirement
         * on the self-test report and a profile without the census pins all
         * fail closed. */
        BQ_PREP_CHECK(bq_retirement_unit_project_pinned(store, unit, &success->built, workspaces, installed, root,
                      profile, S8("self-test"), fixture->driver, fixture->toolchain_root, fixture->broker,
                      fixture->workspaces, projection) == BQ_BAD_REQUEST && projection->owned);
        BQ_PREP_CHECK(bq_retirement_unit_project(store, unit, &success->built, workspaces, installed, &refused) ==
                      BQ_RECIPE_MISMATCH && !refused.owned && !refused.rows);
        BQ_PREP_CHECK(bq_retirement_unit_project_pinned(store, unit, &success->built, workspaces, installed, root,
                      profile, S8("full-census"), fixture->driver, fixture->toolchain_root, fixture->broker,
                      fixture->workspaces, &refused) == BQ_SOURCE_MISMATCH && !refused.owned);
        BQ_PREP_CHECK(bq_retirement_unit_project_pinned(store, unit, &success->built, workspaces, installed, root,
                      string_from_pointer(fixture->base_profile), S8("self-test"), fixture->driver,
                      fixture->toolchain_root, fixture->broker, fixture->workspaces, &refused) == BQ_OK &&
                      bq_retirement_projection_release(&refused));
        char variant[4096];
        char const* missing[] = {"census-rows-sha256=", "performance-rows-sha256="};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(missing); index += 1)
        {
            memcpy(variant, fixture->profile, sizeof(variant));
            char* line = strstr(variant, missing[index]);
            char* end = line ? strchr(line, '\n') : NULL;
            if (end) memmove(line, end + 1, strlen(end + 1) + 1);
            BQ_PREP_CHECK(end && bq_retirement_unit_project_pinned(store, unit, &success->built, workspaces,
                          installed, root, string_from_pointer(variant), S8("self-test"), fixture->driver,
                          fixture->toolchain_root, fixture->broker, fixture->workspaces, &refused) ==
                          BQ_RECIPE_MISMATCH && !refused.owned);
        }
        memcpy(variant, fixture->profile, sizeof(variant));
        bq_prep_test_flip_pin(variant, "performance-rows-sha256=");
        BQ_PREP_CHECK(bq_retirement_unit_project_pinned(store, unit, &success->built, workspaces, installed, root,
                      string_from_pointer(variant), S8("self-test"), fixture->driver, fixture->toolchain_root,
                      fixture->broker, fixture->workspaces, &refused) == BQ_SOURCE_MISMATCH && !refused.owned);
        memcpy(variant, fixture->profile, sizeof(variant));
        bq_prep_test_flip_pin(variant, "validator-report-sha256=");
        BQ_PREP_CHECK(bq_retirement_unit_project_pinned(store, unit, &success->built, workspaces, installed, root,
                      string_from_pointer(variant), S8("self-test"), fixture->driver, fixture->toolchain_root,
                      fixture->broker, fixture->workspaces, &refused) == BQ_SOURCE_MISMATCH && !refused.owned);

        /* begin_service takes only the sealed projection and stays
         * fail-closed; a row changed after the join no longer enters. */
        BqRetirementCorrectness gate = {0};
        BqRetirementHeldBinaries held = {.descriptors = {-1, -1}};
        BQ_PREP_CHECK(bq_retirement_correctness_begin_service(projection, NULL, 0, NULL, NULL, NULL, 0, NULL, 0,
                      &held, &gate) == BQ_RECIPE_MISMATCH && gate.failed && !held.owned);
        projection->rows[7].configuration_sha256[0] ^= 1;
        gate = (BqRetirementCorrectness){0};
        BQ_PREP_CHECK(bq_retirement_correctness_begin_service(projection, NULL, 0, NULL, NULL, NULL, 0, NULL, 0,
                      &held, &gate) == BQ_SOURCE_MISMATCH && gate.failed);

        /* The oracle refuses the changed rows, and the blocked profile, before
         * creating its output directory. */
        struct stat info = {0};
        BqRetirementUnitOracle oracle = {0};
        BQ_PREP_CHECK(bq_retirement_unit_oracle_pinned(unit, projection, workspaces, profile, cancel[0], generous,
                      &oracle) == BQ_SOURCE_MISMATCH && !oracle.owned);
        projection->rows[7].configuration_sha256[0] ^= 1;
        BQ_PREP_CHECK(bq_retirement_unit_oracle(unit, projection, workspaces, cancel[0], generous, &oracle) ==
                      BQ_RECIPE_MISMATCH && !oracle.owned &&
                      fstatat(success->attempt.attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/"
                              BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                      errno == ENOENT);

        /* A cancellation descriptor that is not a read-only close-on-exec
         * FIFO or socket (the write end, a directory, one without CLOEXEC, a
         * standard stream) is a configuration error before any work. */
        int inherited[2] = {-1, -1};
        BQ_PREP_CHECK(pipe2(inherited, O_NONBLOCK) == 0);
        int const invalid[] = {cancel[1], workspaces, inherited[0], STDIN_FILENO, -1};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
        {
            BQ_PREP_CHECK(bq_retirement_unit_oracle_pinned(unit, projection, workspaces, profile, invalid[index],
                          generous, &oracle) == BQ_CONFIGURATION_MISMATCH && !oracle.owned &&
                          fstatat(success->attempt.attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/"
                                  BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                          errno == ENOENT);
        }
        if (inherited[0] >= 0) close(inherited[0]);
        if (inherited[1] >= 0) close(inherited[1]);

        /* Success: authority over the joined rows, the producer loop and all
         * three readiness checks; the rows carry the observed oracle. */
        BQ_PREP_CHECK(bq_retirement_unit_oracle_pinned(unit, projection, workspaces, profile, cancel[0], generous,
                      &oracle) == BQ_OK && oracle.owned && bq_retirement_oracle_authority_ready(&oracle.authority) &&
                      !strcmp(oracle.attempt_sha256, oracle.authority.attempt_sha256) &&
                      bq_retirement_hex(string_from_pointer(oracle.attempt_sha256), 64) &&
                      oracle.authority.observed_rows == BQ_PREP_ORACLE_REFERENCES &&
                      projection->rows[192].independent_oracle_sha256[0] &&
                      projection->rows[193].independent_oracle_sha256[0] &&
                      !projection->rows[194].independent_oracle_sha256[0] &&
                      !projection->rows[16].independent_oracle_sha256[0]);
        char const* outputs[] = {"reference-00000000", "reference-00000001", "reference-receipt-00000001",
                                 "oracle-output-0", "oracle-output-1"};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(outputs); index += 1)
        {
            char path[160];
            int length = snprintf(path, sizeof(path), BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/"
                                  BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY "/%s", outputs[index]);
            BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(path) &&
                          fstatat(success->attempt.attempt, path, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                          S_ISREG(info.st_mode) && info.st_uid == geteuid());
        }
        /* A second oracle over the same attempt and observed rows fails. */
        BqRetirementUnitOracle again = {0};
        BQ_PREP_CHECK(bq_retirement_unit_oracle_pinned(unit, projection, workspaces, profile, cancel[0], generous,
                      &oracle) == BQ_BAD_REQUEST && oracle.owned);
        BQ_PREP_CHECK(bq_retirement_unit_oracle_pinned(unit, projection, workspaces, profile, cancel[0], generous,
                      &again) != BQ_OK && !again.owned);
        /* Design steps 9 and 10, then the coordinator replay. */
        bq_prep_test_unit_ready(fixture, success, &oracle);
        BQ_PREP_CHECK(bq_retirement_unit_oracle_release(&oracle) && !oracle.owned && !oracle.references);
        BQ_PREP_CHECK(bq_prep_test_live_children() == 0);

        /* Tokens in the service translation unit: only the live producer's
         * own pending token reaches a child. */
        BqPrepOraclePair* pairs = calloc(2, sizeof(*pairs));
        BQ_PREP_CHECK(pairs && bq_prep_test_oracle_pair(fixture, success, "oracle-tokens-0", &pairs[0]) &&
                      bq_prep_test_oracle_pair(fixture, success, "oracle-tokens-1", &pairs[1]));
        BqRetirementOracleVerifiedBuild const* token = NULL;
        BqRetirementOracleVerifiedBuild const* foreign = NULL;
        u64 step = bq_phase_clock() + 120ull * 1000000000ull;
        BQ_PREP_CHECK(pairs && bq_retirement_reference_producer_next(&pairs[0].producer, cancel[0], step, &token) &&
                      bq_retirement_reference_producer_next(&pairs[1].producer, cancel[0], step, &foreign));
        if (token && foreign)
        {
            BqRetirementOracleVerifiedBuild forged = *token;
            BqRetirementReferenceRuntime runtime = {0}, refused_runtime = {0};
            BQ_PREP_CHECK(bq_retirement_reference_producer_token_valid(token, &pairs[0].authority) &&
                          !bq_retirement_reference_producer_token_valid(&forged, &pairs[0].authority) &&
                          !bq_retirement_reference_producer_token_valid(foreign, &pairs[0].authority) &&
                          !bq_retirement_reference_producer_token_valid(token, &pairs[1].authority) &&
                          !bq_retirement_reference_producer_runtime(&pairs[0].producer, &forged, &refused_runtime) &&
                          !bq_retirement_reference_producer_runtime(&pairs[0].producer, foreign, &refused_runtime) &&
                          !refused_runtime.command.arguments &&
                          bq_retirement_reference_producer_runtime(&pairs[0].producer, token, &runtime));
            /* A forged token fails before any launch and poisons the attempt. */
            BQ_PREP_CHECK(!bq_retirement_oracle_authority_next(&pairs[0].authority, &forged, &runtime.command,
                                                               runtime.output, cancel[0], step) &&
                          pairs[0].authority.ledger.failed &&
                          fstatat(pairs[0].output, "oracle-output-0", &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                          errno == ENOENT);
            /* A consumed token is stale: a second use fails and poisons. */
            BQ_PREP_CHECK(bq_retirement_reference_producer_runtime(&pairs[1].producer, foreign, &runtime) &&
                          bq_retirement_oracle_authority_next(&pairs[1].authority, foreign, &runtime.command,
                                                              runtime.output, cancel[0], step) &&
                          !bq_retirement_reference_producer_token_valid(foreign, &pairs[1].authority) &&
                          !bq_retirement_reference_producer_runtime(&pairs[1].producer, foreign, &refused_runtime) &&
                          !bq_retirement_oracle_authority_next(&pairs[1].authority, foreign, &runtime.command,
                                                               runtime.output, cancel[0], step) &&
                          pairs[1].authority.ledger.failed &&
                          fstatat(pairs[1].output, "oracle-output-1", &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                          errno == ENOENT);
        }
        for (u32 index = 0; pairs && index < 2; index += 1) BQ_PREP_CHECK(bq_prep_test_oracle_pair_close(&pairs[index]));
        free(pairs);
        BQ_PREP_CHECK(bq_prep_test_live_children() == 0);
    }
    if (success) BQ_PREP_CHECK(bq_prep_test_oracle_attempt_close(success));
    free(success);

    /* Cancellation (the SIGTERM self-pipe, written here by SIGALRM) and
     * deadline expiry while the second reference program spins: the child is
     * killed and reaped, the attempt fails closed and holds no oracle. */
    BQ_PREP_CHECK(ok && bq_prep_test_oracle_install(fixture, true));
    for (u32 trial = 0; ok && trial < 2; trial += 1)
    {
        BqPrepOracleAttempt* hanging = calloc(1, sizeof(*hanging));
        BQ_PREP_CHECK(hanging && bq_prep_test_oracle_attempt(fixture, 82 + trial, cancel[0], hanging));
        struct sigaction handler = {.sa_handler = bq_prep_test_cancel_handler}, prior = {0};
        struct itimerval timer = {.it_value = {4, 0}}, stopped = {{0, 0}, {0, 0}};
        bool armed = !trial && sigemptyset(&handler.sa_mask) == 0 && sigaction(SIGALRM, &handler, &prior) == 0;
        bq_prep_test_cancel_writer = cancel[1];
        if (armed) armed = setitimer(ITIMER_REAL, &timer, NULL) == 0;
        u64 deadline = trial ? bq_phase_clock() + 4000000000ull : bq_phase_clock() + 300000000000ull;
        BqRetirementUnitOracle oracle = {0};
        BqError result = hanging ? bq_retirement_unit_oracle_pinned(&hanging->attempt.unit, &hanging->projection,
            fixture->workspaces_fd, string_from_pointer(fixture->profile), cancel[0], deadline, &oracle) : BQ_IO;
        if (!trial)
        {
            BQ_PREP_CHECK(armed && setitimer(ITIMER_REAL, &stopped, NULL) == 0 && sigaction(SIGALRM, &prior, NULL) == 0);
            bq_prep_test_cancel_writer = -1;
            char drained[8];
            BQ_PREP_CHECK(read(cancel[0], drained, sizeof(drained)) >= 1);
        }
        BQ_PREP_CHECK(result == (trial ? BQ_WORKER_TIMEOUT : BQ_WORKER_CANCEL_SIGNAL) && !oracle.owned &&
                      !oracle.references && !oracle.attempt_sha256[0]);
        BQ_PREP_CHECK(bq_prep_test_live_children() == 0);
        /* The first reference row completed; the loop stopped in the second. */
        struct stat info = {0};
        BQ_PREP_CHECK(hanging && fstatat(hanging->attempt.attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/"
                                         BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY "/oracle-output-0", &info,
                                         AT_SYMLINK_NOFOLLOW) == 0 &&
                      fstatat(hanging->attempt.attempt, BQ_RETIREMENT_BUILD_WORK_DIRECTORY "/"
                              BQ_RETIREMENT_UNIT_REFERENCE_DIRECTORY "/reference-00000001", &info,
                              AT_SYMLINK_NOFOLLOW) == 0);
        if (hanging) BQ_PREP_CHECK(bq_prep_test_oracle_attempt_close(hanging));
        free(hanging);
    }
    if (cancel[0] >= 0) close(cancel[0]);
    if (cancel[1] >= 0) close(cancel[1]);
    if (fixture) bq_prep_test_oracle_teardown(fixture);
    free(fixture);
    BQ_PREP_CHECK(bq_prep_test_open_descriptors() == descriptors);
}

#endif
