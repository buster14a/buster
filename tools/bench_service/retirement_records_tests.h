/* The offline record generators (retirement_records.c) over the worker-unit
 * fixture's installed census: given the declaration equivalent to the
 * fixture's hand-written authorities, the generated row plan and
 * untimed-command contract are byte-identical to the ones the whole campaign
 * runs on, import through bq_retirement_row_plan_import_profile and
 * bq_retirement_worker_untimed_import, and the generated counts preflight the
 * fixture's reviewed budget (tp_retirement_budget_review). Every refusal
 * emits nothing. Included by retirement_prepare_tests.c after the worker-unit
 * fixture, which calls bq_prep_records_test once its profile is complete. */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_RECORDS_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_RECORDS_TESTS_H

/* Edits of the fixture-equivalent declaration. */
typedef enum BqPrepRecordsEdit
{
    BQ_PREP_RECORDS_EXACT,
    BQ_PREP_RECORDS_DROP_TIMED,
    BQ_PREP_RECORDS_SHARED_KEY,
    BQ_PREP_RECORDS_NO_RULES,
    BQ_PREP_RECORDS_RULES_UNORDERED,
    BQ_PREP_RECORDS_EXIT_WITHOUT_FAILURE,
    BQ_PREP_RECORDS_CONTROL,
    BQ_PREP_RECORDS_ELIGIBLE_CONTROL,
    BQ_PREP_RECORDS_SINGLETON_TIMED,
    BQ_PREP_RECORDS_DROP_UNTIMED,
} BqPrepRecordsEdit;

/* The declaration of the fixture's authorities (bq_prep_worker_unit_plan_text
 * and _untimed_text): its installed plan's templates verbatim, template 0
 * for every single compile, the program template (3 + object groups) and
 * runtime template 1 for native runtime rows, each timed object group on its
 * own batch template 2 + g with allocator none, every untimed object group
 * on the untimed batch template with the fixture's target word. control_row
 * is a native, compiler-ineligible object row, or UINT32_MAX for a control
 * naming no row; eligible_row a compiler-eligible one. */
BUSTER_GLOBAL_LOCAL bool bq_prep_records_declaration(BqRetirementRecordsCensus const* census, char const* plan,
    BqPrepRecordsEdit edit, u32 control_row, u32 eligible_row, BqRetirementRowText* text)
{
    char const* templates = strstr(plan, "\ntemplates=");
    char const* rows = templates ? strstr(templates, "\nrows=") : NULL;
    u32 objects = census->timed.object_groups, program = 3u + objects, untimed_template = 2u + objects;
    *text = (BqRetirementRowText){.ok = templates && rows};
    bq_retirement_row_text(text, BQ_RETIREMENT_RECORDS_DECLARATION_HEADER "\n");
    if (text->ok) bq_retirement_records_raw(text, templates + 1, (u64)(rows - templates));
    u32 rules = edit == BQ_PREP_RECORDS_NO_RULES ? 0u : 3u * 12u * 2u;
    bq_retirement_row_text(text, "compile-rules=%u\n", rules);
    for (u32 rule = 0; rule < rules; rule += 1)
    {
        u32 key = edit == BQ_PREP_RECORDS_RULES_UNORDERED ? rules - 1u - rule : rule;
        u32 stage = key / 24u, target = key / 2u % 12u + 1u, runtime = key % 2u;
        char compile[16];
        snprintf(compile, sizeof(compile), "%u", runtime ? program : 0u);
        bq_retirement_row_text(text, "compile=%s %u %s %s\n", tp_retirement_budget_stage_names[stage], target, compile,
                               runtime ? "1" : "-");
    }
    u32 singleton = UINT32_MAX;
    for (u32 group = 0; group < census->timed.count; group += 1)
        if (!census->timed.object[group] && singleton == UINT32_MAX) singleton = group;
    u32 declared = objects - (edit == BQ_PREP_RECORDS_DROP_TIMED && objects);
    bq_retirement_row_text(text, "timed-groups=%u\n", declared + (edit == BQ_PREP_RECORDS_SINGLETON_TIMED));
    for (u32 group = 0, batch = 0; group < census->timed.count; group += 1)
    {
        bool control = batch == 0 && (edit == BQ_PREP_RECORDS_CONTROL || edit == BQ_PREP_RECORDS_ELIGIBLE_CONTROL);
        if (edit == BQ_PREP_RECORDS_SINGLETON_TIMED && group == singleton)
            bq_retirement_row_text(text, "timed=%u 2 none 0 0\n", group);
        if (!census->timed.object[group]) continue;
        if (!(edit == BQ_PREP_RECORDS_DROP_TIMED && batch == 0))
            bq_retirement_row_text(text, "timed=%u %u none %u %u\n", group,
                                   edit == BQ_PREP_RECORDS_SHARED_KEY ? 2u : 2u + batch,
                                   edit == BQ_PREP_RECORDS_EXIT_WITHOUT_FAILURE && batch == 0 ? 1u : control ? 1u : 0u,
                                   control ? 1u : 0u);
        u32 named = edit == BQ_PREP_RECORDS_CONTROL ? control_row : eligible_row;
        if (control && named != UINT32_MAX)
            bq_retirement_row_text(text, "control=%u rejected driver.analysis -\n", named);
        else if (control)
            bq_retirement_row_text(text, "control=- rejected driver.analysis tests/records-control.c\n");
        batch += 1;
    }
    u32 untimed = census->untimed.object_groups - (edit == BQ_PREP_RECORDS_DROP_UNTIMED && census->untimed.object_groups);
    bq_retirement_row_text(text, "untimed-groups=%u\n", untimed);
    for (u32 group = 0, seen = 0; group < census->untimed.count; group += 1)
    {
        if (!census->untimed.object[group]) continue;
        if (!(edit == BQ_PREP_RECORDS_DROP_UNTIMED && seen == 0))
            bq_retirement_row_text(text, "untimed=%u %u %s none\n", group, untimed_template,
                                   BQ_PREP_WORKER_UNIT_UNTIMED_TARGET);
        seen += 1;
    }
    return text->ok;
}

/* The last generator run's diagnostic. */
BUSTER_GLOBAL_LOCAL char bq_prep_records_diagnostic[BQ_RETIREMENT_RECORDS_DIAGNOSTIC];

/* One generator run over a fresh declaration of edit: 0 plan, 1 untimed
 * (over plan_bytes), 2 counts. The output is in *text on success. */
BUSTER_GLOBAL_LOCAL bool bq_prep_records_generate(BqRetirementRecordsCensus const* census, char const* plan,
    BqPrepRecordsEdit edit, u32 control_row, u32 eligible_row, u32 kind, char const* cpu_model, u32 cpu,
    char const* plan_bytes, u32 plan_length, BqRetirementRowText* text)
{
    BqRetirementRowText declared = {0};
    BqRetirementRecordsDeclaration declaration = {0};
    TpRetirementCampaignBudget budget = bq_campaign_service_budget();
    char diagnostic[BQ_RETIREMENT_RECORDS_DIAGNOSTIC] = {0};
    *text = (BqRetirementRowText){0};
    bool ok = bq_prep_records_declaration(census, plan, edit, control_row, eligible_row, &declared) &&
              bq_retirement_records_declaration_parse((u8 const*)declared.bytes, (u32)declared.length, &declaration,
                                                      diagnostic);
    ok = ok && (kind == 0 ? bq_retirement_records_row_plan(census, &declaration, &budget, cpu_model, cpu, text, diagnostic) :
                kind == 1 ? bq_retirement_records_untimed(census, &declaration, (u8 const*)plan_bytes, plan_length, text,
                                                          diagnostic) :
                bq_retirement_records_counts(census, &declaration, 60, text, diagnostic));
    if (!ok && getenv("BQ_PREP_RECORDS_VERBOSE")) fprintf(stderr, "RETIREMENT_PREP records edit %u kind %u: %s\n",
                                                          (unsigned)edit, kind, diagnostic);
    /* A refusal leaves nothing behind. */
    if (!ok) BQ_PREP_CHECK(!text->bytes && !text->length && diagnostic[0]);
    memcpy(bq_prep_records_diagnostic, diagnostic, sizeof(diagnostic));
    bq_retirement_records_declaration_release(&declaration);
    free(declared.bytes);
    return ok;
}

/* The CLI seam end to end: files in, the record on a temporary stream. */
BUSTER_GLOBAL_LOCAL BqError bq_prep_records_cli(char** argv, int argc, char** output, u32* length)
{
    FILE* stream = tmpfile();
    FILE* sink = fopen("/dev/null", "w");
    BqError result = stream && sink ? bq_retirement_records_run(argc, argv, S8("self-test"), stream, sink) : BQ_IO;
    long size = stream && fseek(stream, 0, SEEK_END) == 0 ? ftell(stream) : -1;
    *output = size >= 0 ? malloc((size_t)size + 1u) : NULL;
    *length = 0;
    if (*output && fseek(stream, 0, SEEK_SET) == 0 && fread(*output, 1, (size_t)size, stream) == (size_t)size)
    {
        (*output)[size] = 0;
        *length = (u32)size;
    }
    if (stream) fclose(stream);
    if (sink) fclose(sink);
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_records_file(char const* directory, char const* name, char const* bytes, u64 length,
    char path[256])
{
    int written = snprintf(path, 256, "%s/%s", directory, name);
    bool ok = written > 0 && written < 256 && (unlink(path) == 0 || errno == ENOENT) &&
              bq_prep_test_write_bytes(path, bytes, (u32)length);
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_prep_records_test(BqPrepOracleFixture* fixture, BqRetirementProjection const* projection,
    BqJob const* job, BqRetirementRowPlan const* row_plan)
{
    String8 profile = string_from_pointer(fixture->profile);
    BqRetirementRecordsCensus census = {0};
    int recipes = openat(fixture->installed_fd, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    u32 plan_length = 0, untimed_length = 0;
    char* plan = recipes >= 0 ? bq_prep_worker_unit_slurp(recipes, BQ_RETIREMENT_ROW_PLAN_NAME, &plan_length) : NULL;
    char* untimed = recipes >= 0 ? bq_prep_worker_unit_slurp(recipes, BQ_RETIREMENT_WORKER_UNTIMED_NAME, &untimed_length) :
                    NULL;
    if (recipes >= 0) close(recipes);
    /* The generators' census is the unit's projection: same rows, same seal. */
    BQ_PREP_CHECK(plan && untimed &&
                  bq_retirement_records_census(fixture->installed_fd, profile, S8("self-test"), &census) == BQ_OK &&
                  census.projection.prepared.rows == projection->prepared.rows &&
                  census.projection.prepared.object_rows == projection->prepared.object_rows &&
                  !memcmp(census.projection.population_sha256, projection->population_sha256, SHA256_HEX_CAPACITY) &&
                  !memcmp(census.projection.prepared.census_sha256, projection->prepared.census_sha256,
                          SHA256_HEX_CAPACITY));
    bq_retirement_records_census_release(&census);
    BQ_PREP_CHECK(bq_retirement_records_census(fixture->installed_fd, profile, S8("full-census"), &census) ==
                      BQ_SOURCE_MISMATCH && !census.projection.rows);
    bool ready = plan && untimed &&
                 bq_retirement_records_census(fixture->installed_fd, profile, S8("self-test"), &census) == BQ_OK;
    u32 control_row = UINT32_MAX, eligible_row = UINT32_MAX;
    for (u32 row = 0; ready && row < census.projection.prepared.rows; row += 1)
    {
        BqRetirementTrustedRow const* trusted = census.projection.rows + row;
        if (!trusted->compiler_eligible && trusted->stage == BQ_RETIREMENT_STAGE_OBJECT &&
            trusted->target == census.projection.prepared.native_target && control_row == UINT32_MAX)
            control_row = row;
        if (trusted->compiler_eligible && eligible_row == UINT32_MAX) eligible_row = row;
    }
    char const* cpu_model = row_plan->cpu_model_sha256;
    u32 cpu = row_plan->cpu;

    /* (a) The fixture-equivalent declaration regenerates both authorities
     * byte for byte, deterministically. */
    BqRetirementRowText generated = {0}, again = {0}, contract = {0}, counts = {0};
    BQ_PREP_CHECK(ready && bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_EXACT, control_row, eligible_row, 0,
                                                    cpu_model, cpu, NULL, 0, &generated) &&
                  generated.length == plan_length && !memcmp(generated.bytes, plan, plan_length));
    BQ_PREP_CHECK(ready && bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_EXACT, control_row, eligible_row, 0,
                                                    cpu_model, cpu, NULL, 0, &again) &&
                  again.length == generated.length && generated.bytes &&
                  !memcmp(again.bytes, generated.bytes, generated.length));
    BQ_PREP_CHECK(ready && generated.bytes &&
                  bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_EXACT, control_row, eligible_row, 1,
                                           cpu_model, cpu, generated.bytes, (u32)generated.length, &contract) &&
                  contract.length == untimed_length && !memcmp(contract.bytes, untimed, untimed_length));
    /* ... and both import through the real importers from the pinned files. */
    char pin[128] = {0};
    BqRetirementRowPlan imported = {0};
    BQ_PREP_CHECK(generated.bytes &&
                  bq_prep_worker_unit_install(fixture->recipes, BQ_RETIREMENT_ROW_PLAN_NAME, generated.bytes,
                                              (u32)generated.length, "row-plan-sha256=", pin) &&
                  strstr(fixture->profile, pin) &&
                  bq_retirement_row_plan_import_profile(fixture->installed_fd, profile, job, projection, &imported) ==
                      BQ_OK &&
                  !strcmp(imported.aa_second_commands_sha256, row_plan->aa_second_commands_sha256));
    Arena* arena = arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 30, .flags = {.no_pool = 1}});
    BqRetirementWorkerUntimedContract parsed = {0};
    BQ_PREP_CHECK(arena && imported.owned && contract.bytes &&
                  bq_prep_worker_unit_install(fixture->recipes, BQ_RETIREMENT_WORKER_UNTIMED_NAME, contract.bytes,
                                              (u32)contract.length, BQ_RETIREMENT_WORKER_UNTIMED_PIN, pin) &&
                  strstr(fixture->profile, pin) &&
                  bq_retirement_worker_untimed_import(arena, fixture->installed_fd, profile, &imported, &census.untimed,
                                                      &parsed) == BQ_OK &&
                  parsed.group_count == census.untimed.object_groups);
    if (arena) arena_destroy(arena, 1);
    BqError refused = imported.owned ? BQ_OK : BQ_RECIPE_MISMATCH;
    bq_retirement_row_plan_release(&imported);
    BQ_PREP_CHECK(refused == BQ_OK);

    /* (b) The counts preflight the fixture's reviewed budget; the timed
     * groups are the campaign's. */
    TpRetirementBudgetOwnedCounts owned = {0};
    TpRetirementBudgetPreflight preflight = {0};
    TpRetirementCampaignBudget budget = bq_campaign_service_budget();
    char diagnostic[TP_RETIREMENT_BUDGET_DIAGNOSTIC] = {0};
    u32 runtime = 0;
    for (u32 row = 0; ready && row < census.projection.prepared.rows; row += 1)
        runtime += bq_retirement_row_native_runtime(census.projection.rows + row, census.projection.prepared.native_target);
    BQ_PREP_CHECK(ready && bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_EXACT, control_row, eligible_row, 2,
                                                    cpu_model, cpu, NULL, 0, &counts) &&
                  tp_retirement_budget_counts_parse(counts.bytes, (size_t)counts.length, &owned) &&
                  owned.counts.timed.count == census.timed.count && owned.counts.untimed.count == census.untimed.count &&
                  owned.counts.runtime_rows == runtime && owned.counts.pairs == 60 &&
                  !memcmp(owned.population_sha256, projection->population_sha256, SHA256_HEX_CAPACITY) &&
                  tp_retirement_budget_review(&budget, &owned.counts, &preflight, diagnostic) && preflight.fits);
    tp_retirement_budget_counts_release(&owned);

    /* (c) Refusals: an uncovered, singleton or dropped group, a shared batch
     * key, a missing or unordered compile rule, an exit status without a
     * failing control, a control on a compiler-eligible row, bad host facts,
     * and a plan with another digest for the untimed contract. */
    BqRetirementRowText refused_text = {0};
    static struct
    {
        BqPrepRecordsEdit edit;
        char const* expect;
    } const plan_refusals[] = {
        {BQ_PREP_RECORDS_DROP_TIMED, "timed object group 0 (first row"},
        {BQ_PREP_RECORDS_NO_RULES, "has no compile rule"},
        {BQ_PREP_RECORDS_RULES_UNORDERED, "is not canonical " BQ_RETIREMENT_RECORDS_DECLARATION_HEADER},
        {BQ_PREP_RECORDS_EXIT_WITHOUT_FAILURE, "must be nonzero exactly when a control fails"},
        {BQ_PREP_RECORDS_ELIGIBLE_CONTROL, "is not a native, compiler-ineligible object row"},
        {BQ_PREP_RECORDS_SHARED_KEY, "share a batch key"},
        {BQ_PREP_RECORDS_SINGLETON_TIMED, "is a singleton, not an object group"},
    };
    for (u32 index = 0; ready && index < BUSTER_ARRAY_LENGTH(plan_refusals); index += 1)
    {
        bool applies = plan_refusals[index].edit == BQ_PREP_RECORDS_SHARED_KEY ? census.timed.object_groups > 1 :
                       plan_refusals[index].edit == BQ_PREP_RECORDS_SINGLETON_TIMED ?
                       census.timed.count > census.timed.object_groups : census.timed.object_groups > 0;
        BQ_PREP_CHECK(applies && !bq_prep_records_generate(&census, plan, plan_refusals[index].edit, control_row,
                                                           eligible_row, 0, cpu_model, cpu, NULL, 0, &refused_text) &&
                      strstr(bq_prep_records_diagnostic, plan_refusals[index].expect));
    }
    BQ_PREP_CHECK(ready && !bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_EXACT, control_row, eligible_row, 0,
                                                     "not-a-digest", cpu, NULL, 0, &refused_text) &&
                  strstr(bq_prep_records_diagnostic, "64 lowercase hex") &&
                  !bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_EXACT, control_row, eligible_row, 0, cpu_model,
                                            CPU_SETSIZE, NULL, 0, &refused_text));
    BQ_PREP_CHECK(ready && census.untimed.object_groups && generated.bytes &&
                  !bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_DROP_UNTIMED, control_row, eligible_row,
                                            1, cpu_model, cpu, generated.bytes, (u32)generated.length, &refused_text) &&
                  strstr(bq_prep_records_diagnostic, "untimed object group"));
    /* A plan naming another census digest (or with any byte changed) does
     * not bind to this census. */
    char* forged = generated.bytes ? malloc(generated.length + 1u) : NULL;
    char* census_line = forged ? (memcpy(forged, generated.bytes, generated.length + 1u), strstr(forged, "\ncensus=")) :
                        NULL;
    if (census_line) census_line[8] = census_line[8] == '0' ? '1' : '0';
    BQ_PREP_CHECK(census_line && !bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_EXACT, control_row,
                                                           eligible_row, 1, cpu_model, cpu, forged,
                                                           (u32)generated.length, &refused_text) &&
                  strstr(bq_prep_records_diagnostic, "does not import against this census"));
    free(forged);
    /* A declared control (a native, compiler-ineligible object row, or one
     * naming no row when the census has none) yields a plan that imports,
     * appends the control to the group, marks a named row a batch control
     * and fails the batch's exit status. */
    BQ_PREP_CHECK(!ready || census.timed.object_groups);
    if (ready && census.timed.object_groups)
    {
        u32 first = 0;
        while (!census.timed.object[first]) first += 1;
        u32 members = census.timed.first[first + 1u] - census.timed.first[first];
        BqRetirementRowText controlled = {0};
        BqRetirementRowPlan decoded = {0};
        BQ_PREP_CHECK(bq_prep_records_generate(&census, plan, BQ_PREP_RECORDS_CONTROL, control_row, eligible_row, 0,
                                               cpu_model, cpu, NULL, 0, &controlled) &&
                      bq_retirement_row_plan_parse((u8 const*)controlled.bytes, (u32)controlled.length,
                                                   &census.projection, &decoded) == BQ_OK &&
                      (control_row == UINT32_MAX || decoded.completed[control_row].batch_control) &&
                      decoded.groups[0].exit_status == 1 &&
                      decoded.groups[0].input_count == members + 1u &&
                      decoded.groups[0].metrics_bytes_max ==
                          budget.metrics_header_bytes + (u64)(members + 1u) * budget.metrics_input_bytes &&
                      decoded.inputs[decoded.groups[0].input_count - 1u].row ==
                          (control_row == UINT32_MAX ? TP_RETIREMENT_BATCH_NO_ROW : control_row) &&
                      !decoded.inputs[decoded.groups[0].input_count - 1u].member);
        bq_retirement_row_plan_release(&decoded);
        free(controlled.bytes);
    }

    /* (d) The CLI seam: files in, exactly the record out ("-" is the
     * stream); a refusal writes nothing. */
    BqRetirementRowText declared = {0};
    char directory[] = "/tmp/bq-records-XXXXXX";
    char profile_path[256], declaration_path[256], budget_path[256], plan_path[256], cpu_text[16];
    char budget_text[TP_RETIREMENT_BUDGET_BYTES];
    size_t budget_size = tp_retirement_budget_encode(&budget, budget_text, sizeof(budget_text));
    snprintf(cpu_text, sizeof(cpu_text), "%u", cpu);
    bool files = ready && generated.bytes && counts.bytes && budget_size && mkdtemp(directory) &&
                 bq_prep_records_declaration(&census, plan, BQ_PREP_RECORDS_EXACT, control_row, eligible_row, &declared) &&
                 bq_prep_records_file(directory, "records-profile", fixture->profile, strlen(fixture->profile),
                                      profile_path) &&
                 bq_prep_records_file(directory, "records-declaration", declared.bytes, declared.length,
                                      declaration_path) &&
                 bq_prep_records_file(directory, "records-budget", budget_text, budget_size, budget_path) &&
                 bq_prep_records_file(directory, "records-plan", generated.bytes, generated.length, plan_path);
    BQ_PREP_CHECK(files);
    char* output = NULL;
    u32 output_length = 0;
    char* plan_argv[] = {"row-plan", fixture->installed, profile_path, declaration_path, budget_path,
                         (char*)cpu_model, cpu_text, "-"};
    BQ_PREP_CHECK(files && bq_prep_records_cli(plan_argv, 8, &output, &output_length) == BQ_OK &&
                  output_length == generated.length && !memcmp(output, generated.bytes, output_length));
    free(output);
    char* untimed_argv[] = {"untimed-commands", fixture->installed, profile_path, declaration_path, plan_path, "-"};
    BQ_PREP_CHECK(files && bq_prep_records_cli(untimed_argv, 6, &output, &output_length) == BQ_OK &&
                  output_length == untimed_length && !memcmp(output, untimed, untimed_length));
    free(output);
    char* counts_argv[] = {"budget-counts", fixture->installed, profile_path, declaration_path, "60", "-"};
    BQ_PREP_CHECK(files && bq_prep_records_cli(counts_argv, 6, &output, &output_length) == BQ_OK &&
                  output_length == counts.length && !memcmp(output, counts.bytes, output_length));
    free(output);
    /* The pair count is the profile's campaign-pairs=: another count, or a
     * profile without the pin, refuses. */
    char* other_pairs[] = {"budget-counts", fixture->installed, profile_path, declaration_path, "62", "-"};
    BQ_PREP_CHECK(files && bq_prep_records_cli(other_pairs, 6, &output, &output_length) == BQ_RECIPE_MISMATCH &&
                  output_length == 0);
    free(output);
    char unpinned_path[256] = {0}, unpinned[4096];
    char const* pairs_line = strstr(fixture->profile, BQ_RETIREMENT_UNIT_CAMPAIGN_PAIRS_KEY);
    char const* pairs_end = pairs_line ? strchr(pairs_line, '\n') : NULL;
    int unpinned_length = pairs_end ? snprintf(unpinned, sizeof(unpinned), "%.*s%s", (int)(pairs_line - fixture->profile),
                                               fixture->profile, pairs_end + 1) : -1;
    char* no_pairs[] = {"budget-counts", fixture->installed, unpinned_path, declaration_path, "60", "-"};
    BQ_PREP_CHECK(files && unpinned_length > 0 && (size_t)unpinned_length < sizeof(unpinned) &&
                  bq_prep_records_file(directory, "records-unpinned", unpinned, (u64)unpinned_length, unpinned_path) &&
                  bq_prep_records_cli(no_pairs, 6, &output, &output_length) == BQ_RECIPE_MISMATCH && output_length == 0);
    free(output);
    /* An OUTPUT file is written exclusively: created read-only with exactly
     * the record, never replaced (a second run refuses and leaves it), and
     * no temporary sibling remains either way. */
    char exclusive[300];
    snprintf(exclusive, sizeof(exclusive), "%s/row-plan", directory);
    char* file_argv[] = {"row-plan", fixture->installed, profile_path, declaration_path, budget_path, (char*)cpu_model,
                         cpu_text, exclusive};
    struct stat published = {0};
    u32 published_length = 0;
    int listing = files ? open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    BQ_PREP_CHECK(files && bq_prep_records_cli(file_argv, 8, &output, &output_length) == BQ_OK && output_length == 0 &&
                  stat(exclusive, &published) == 0 && (published.st_mode & 0777) == 0444);
    free(output);
    char* written = listing >= 0 ? bq_prep_worker_unit_slurp(listing, "row-plan", &published_length) : NULL;
    BQ_PREP_CHECK(written && published_length == generated.length && !memcmp(written, generated.bytes, published_length));
    free(written);
    BQ_PREP_CHECK(files && bq_prep_records_cli(file_argv, 8, &output, &output_length) == BQ_IO && output_length == 0);
    free(output);
    written = listing >= 0 ? bq_prep_worker_unit_slurp(listing, "row-plan", &published_length) : NULL;
    BQ_PREP_CHECK(written && published_length == generated.length && !memcmp(written, generated.bytes, published_length));
    free(written);
    DIR* entries = listing >= 0 ? fdopendir(dup(listing)) : NULL;
    u32 temporaries = 0;
    for (struct dirent* entry = entries ? readdir(entries) : NULL; entry; entry = readdir(entries))
        temporaries += strstr(entry->d_name, ".tmp-") != NULL;
    BQ_PREP_CHECK(entries && temporaries == 0);
    if (entries) closedir(entries);
    if (listing >= 0) close(listing);
    /* Lane D's budget writer through the same entry: the canonical record's
     * values as a reviewed input encode back to the record, which preflights
     * against the counts regenerated from the census; counts naming another
     * declaration (stale) and a record that is not canonical refuse. */
    char input_path[256] = {0}, counts_path[256] = {0}, stale_path[256] = {0}, input[TP_RETIREMENT_BUDGET_BYTES + 64];
    char const* values = budget_size ? strchr(strchr(budget_text, '\n') + 1, '\n') + 1 : NULL;
    int input_length = values ? snprintf(input, sizeof(input), "schema=%s\n# fixture values\n%.*s",
                                         TP_RETIREMENT_BUDGET_INPUT_SCHEMA,
                                         (int)(budget_size - (size_t)(values - budget_text)), values) : -1;
    char* stale = counts.bytes ? malloc(counts.length + 1u) : NULL;
    char* stale_digest = stale ? (memcpy(stale, counts.bytes, counts.length + 1u), strstr(stale, "\ndeclaration=")) : NULL;
    if (stale_digest) stale_digest[13] = stale_digest[13] == '0' ? '1' : '0';
    bool budget_files = files && stale_digest && input_length > 0 && (size_t)input_length < sizeof(input) &&
                        bq_prep_records_file(directory, "records-input", input, (u64)input_length, input_path) &&
                        bq_prep_records_file(directory, "records-counts", counts.bytes, counts.length, counts_path) &&
                        bq_prep_records_file(directory, "records-stale", stale, counts.length, stale_path);
    free(stale);
    char* encode_argv[] = {"budget-encode", input_path, counts_path, "-"};
    BQ_PREP_CHECK(budget_files && bq_prep_records_cli(encode_argv, 4, &output, &output_length) == BQ_OK &&
                  output_length == budget_size && !memcmp(output, budget_text, budget_size));
    free(output);
    char* preflight_argv[] = {"budget-preflight", fixture->installed, profile_path, declaration_path, budget_path,
                              counts_path};
    BQ_PREP_CHECK(budget_files && bq_prep_records_cli(preflight_argv, 6, &output, &output_length) == BQ_OK &&
                  !strncmp(output, "budget-sha256=", 14) && strstr(output, "\nfits=1\n"));
    free(output);
    char* stale_argv[] = {"budget-preflight", fixture->installed, profile_path, declaration_path, budget_path,
                          stale_path};
    BQ_PREP_CHECK(budget_files && bq_prep_records_cli(stale_argv, 6, &output, &output_length) == BQ_RECIPE_MISMATCH &&
                  output_length == 0);
    free(output);
    char* refused_argv[] = {"budget-preflight", fixture->installed, profile_path, declaration_path, input_path,
                            counts_path};
    BQ_PREP_CHECK(budget_files && bq_prep_records_cli(refused_argv, 6, &output, &output_length) != BQ_OK &&
                  output_length == 0);
    free(output);
    /* The untimed contract refuses the budget file as a plan, the plan
     * generator a malformed CPU, and the public entry this self-test census
     * (it requires the full census): each with nothing on output. */
    char* wrong_plan[] = {"untimed-commands", fixture->installed, profile_path, declaration_path, budget_path, "-"};
    BQ_PREP_CHECK(files && bq_prep_records_cli(wrong_plan, 6, &output, &output_length) == BQ_RECIPE_MISMATCH &&
                  output_length == 0);
    free(output);
    char* wrong_cpu[] = {"row-plan", fixture->installed, profile_path, declaration_path, budget_path, (char*)cpu_model,
                         "01", "-"};
    BQ_PREP_CHECK(files && bq_prep_records_cli(wrong_cpu, 8, &output, &output_length) == BQ_BAD_REQUEST &&
                  output_length == 0);
    free(output);
    FILE* stream = tmpfile();
    FILE* sink = fopen("/dev/null", "w");
    BQ_PREP_CHECK(files && stream && sink && bq_retirement_records_cli(8, plan_argv, stream, sink) == BQ_SOURCE_MISMATCH &&
                  fseek(stream, 0, SEEK_END) == 0 && ftell(stream) == 0 &&
                  bq_retirement_records_cli(3, plan_argv, stream, sink) == BQ_BAD_REQUEST && ftell(stream) == 0);
    if (stream) fclose(stream);
    if (sink) fclose(sink);
    char const* const names[] = {profile_path, declaration_path, budget_path, plan_path, unpinned_path, exclusive,
                                 input_path, counts_path, stale_path};
    for (u32 index = 0; files && budget_files && index < BUSTER_ARRAY_LENGTH(names); index += 1)
        BQ_PREP_CHECK(unlink(names[index]) == 0);
    if (files && budget_files) BQ_PREP_CHECK(rmdir(directory) == 0);
    free(declared.bytes);
    free(generated.bytes);
    free(again.bytes);
    free(contract.bytes);
    free(counts.bytes);
    free(plan);
    free(untimed);
    bq_retirement_records_census_release(&census);
}
#endif
