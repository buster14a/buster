/* Offline generators of the #881 campaign authorities (lanes B and D): the
 * row plan (BQ-RETIREMENT-ROW-PLAN-V1, pinned by row-plan-sha256=), the
 * untimed-command contract (BQ-RETIREMENT-UNTIMED-COMMANDS-V1, pinned by
 * untimed-commands-sha256=) and the frozen counts the campaign budget is
 * preflighted against (tp-retirement-budget-counts-v1), and the entry to lane
 * D's budget writer (budget-encode, budget-preflight: tp_retirement_budget_cli
 * in ../throughput/retirement_budget_tool.h).
 *
 * Ownership: turning the pinned census, one reviewed declaration and the
 * host facts into those records. Nothing here admits anything or chooses a
 * value: the census rows, partitions and fixtures come from the installed
 * census through the unit's own derivation (bq_retirement_census_population,
 * bq_retirement_documents_population, bq_retirement_documents_partition); the
 * templates, compile rules, allocators, controls and untimed targets are the
 * reviewed declaration's; the metrics bounds are the reviewed budget's; the
 * CPU model digest and logical CPU are host facts the caller measured on the
 * service host. Each generator parses its own output with the importer's
 * parser (bq_retirement_row_plan_parse, bq_retirement_worker_untimed_parse)
 * before emitting it, so a record that would not import is never written.
 * Compiled only into the service translation unit (main.c, after
 * retirement_worker_campaign.c).
 *
 * Entry point: bq_retirement_records_cli (`retirement-records row-plan |
 * untimed-commands | budget-counts | budget-encode | budget-preflight`;
 * bq_retirement_records_run is its seam
 * with the census profile the fixture's self-test census declares), over
 * bq_retirement_records_census,
 * bq_retirement_records_declaration_parse, bq_retirement_records_row_plan,
 * bq_retirement_records_untimed and bq_retirement_records_counts, which the
 * preparation fixture also calls with a self-test census profile.
 *
 * Map: BqRetirementRecordsCensus, BqRetirementRecordsDeclaration (its
 * BqRetirementRecordsRule, BqRetirementRecordsTimed, BqRetirementRecordsControl
 * and BqRetirementRecordsUntimed), bq_retirement_records_raw (verbatim bytes
 * into a BqRetirementRowText), bq_retirement_records_fixture,
 * bq_retirement_records_cover (declaration against a partition),
 * bq_retirement_records_pairs (the profile's campaign-pairs=),
 * bq_retirement_records_publish (exclusive OUTPUT), bq_retirement_records_generate
 * and bq_retirement_records_encode (the subcommands).
 */

#define BQ_RETIREMENT_RECORDS_DECLARATION_HEADER "BQ-RETIREMENT-ROW-PLAN-DECLARATION-V1"
#define BQ_RETIREMENT_RECORDS_DECLARATION_BYTES_CAP (16u * 1024u * 1024u)
#define BQ_RETIREMENT_RECORDS_PROFILE_BYTES_CAP (64u * 1024u)
/* One rule per (stage, target, runtime-or-not). */
#define BQ_RETIREMENT_RECORDS_RULES_CAP (3u * 12u * 2u)
#define BQ_RETIREMENT_RECORDS_FIXTURE_CAP 513u
#define BQ_RETIREMENT_RECORDS_DIAGNOSTIC 512u
/* Every generated leaf fits this. */
#define BQ_RETIREMENT_RECORDS_LEAF_CAP 40u
/* An OUTPUT path and its temporary sibling. */
#define BQ_RETIREMENT_RECORDS_PATH_CAP 4096u

/* The pinned census as the unit derives it (job and attempt zero), its
 * performance-row population and the validator's two partitions. */
typedef struct BqRetirementRecordsCensus
{
    BqRetirementProjection projection;
    BqRetirementDocumentPopulation population;
    BqRetirementDocumentPartition timed, untimed;
} BqRetirementRecordsCensus;

BUSTER_GLOBAL_LOCAL void bq_retirement_records_census_release(BqRetirementRecordsCensus* census)
{
    if (census)
    {
        bq_retirement_projection_release(&census->projection);
        bq_retirement_documents_population_release(&census->population);
        bq_retirement_documents_partition_release(&census->timed);
        bq_retirement_documents_partition_release(&census->untimed);
    }
}

/* The census under installed/recipes/ checked against profile's pins
 * (census_profile is the validator profile the report must declare,
 * full-census in production). BQ_RECIPE_MISMATCH without the pins,
 * BQ_CONFIGURATION_MISMATCH for unreadable files, BQ_SOURCE_MISMATCH for a
 * census the unit's derivation refuses. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_records_census(int installed, String8 profile, String8 census_profile,
    BqRetirementRecordsCensus* census)
{
    *census = (BqRetirementRecordsCensus){0};
    BqRetirementPrepared prepared = {.native_target = BQ_RETIREMENT_UNIT_NATIVE_TARGET};
    BqError result = installed >= 0 && bq_retirement_profile_sha(profile, S8("support-declaration-sha256="),
                                                                 prepared.support_sha256) &&
                     bq_retirement_profile_sha(profile, S8("census-rows-sha256="), prepared.census_sha256) ?
                     BQ_OK : BQ_RECIPE_MISMATCH;
    BqRetirementCensusFiles files;
    for (u32 index = 0; index < BQ_RETIREMENT_CENSUS_FILE_COUNT; index += 1) files.descriptors[index] = -1;
    bool opened = false;
    if (result == BQ_OK)
    {
        result = bq_retirement_unit_census_open(installed, &files);
        opened = result == BQ_OK;
    }
    BqRetirementTrustedRow* rows = NULL;
    char population[SHA256_HEX_CAPACITY] = {0}, evidence[SHA256_HEX_CAPACITY] = {0};
    char compiler[SHA256_HEX_CAPACITY] = {0}, baseline[SHA256_HEX_CAPACITY] = {0};
    if (result == BQ_OK)
        result = bq_retirement_census_population(&files, profile, census_profile, &prepared, &rows, population, evidence,
                                                 compiler, baseline);
    if (opened && !bq_retirement_unit_census_close(&files) && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK)
    {
        census->projection = (BqRetirementProjection){.prepared = prepared, .rows = rows, .owned = 1};
        memcpy(census->projection.population_sha256, population, SHA256_HEX_CAPACITY);
        memcpy(census->projection.evidence_sha256, evidence, SHA256_HEX_CAPACITY);
        rows = NULL;
        result = bq_retirement_documents_population(installed, profile, census->projection.rows, prepared.rows,
                                                    prepared.native_target, &census->population);
    }
    free(rows);
    if (result == BQ_OK && !(bq_retirement_documents_partition(&census->population, 0, &census->timed) &&
                             bq_retirement_documents_partition(&census->population, 1, &census->untimed)))
        result = BQ_IO;
    if (result != BQ_OK) bq_retirement_records_census_release(census);
    return result;
}

/* One compile rule: rows of this stage and target (with or without a native
 * runtime obligation) compile with `compile` and run with `runtime`
 * (BQ_RETIREMENT_ROW_PLAN_NONE for rows without one). */
typedef struct BqRetirementRecordsRule
{
    u32 stage, target, compile, runtime;
} BqRetirementRecordsRule;

/* One status-checked control of a timed group: a native, compiler-ineligible
 * object row, or TP_RETIREMENT_BATCH_NO_ROW with its own fixture. */
typedef struct BqRetirementRecordsControl
{
    char const* status;
    char const* error;
    char const* fixture;
    u32 row;
} BqRetirementRecordsControl;

/* One timed object group of the validator's timed partition. */
typedef struct BqRetirementRecordsTimed
{
    char const* allocator;
    u32 partition, template_index, exit_status, first_control, control_count;
} BqRetirementRecordsTimed;

/* One untimed object group of the validator's untimed partition. */
typedef struct BqRetirementRecordsUntimed
{
    char const* target;
    char const* allocator;
    u32 partition, template_index;
} BqRetirementRecordsUntimed;

/* The reviewed declaration, every string pointing into text:
 *   BQ-RETIREMENT-ROW-PLAN-DECLARATION-V1
 *   templates=<t> and the template blocks, exactly as the row plan has them
 *   compile-rules=<n>
 *   compile=<object|link|self-host-stage1> <target> <compile-template> <-|runtime-template>
 *   timed-groups=<n>
 *   timed=<timed-partition-group> <batch-template> <allocator> <exit-status> <controls>
 *   control=<row|-> <status> <error> <-|fixture>          (controls times)
 *   untimed-groups=<n>
 *   untimed=<untimed-partition-group> <batch-template> <target-word> <allocator>
 * Rules ascend by (stage, target, runtime-or-not); groups ascend by partition
 * group. templates_offset/templates_length is the verbatim template section;
 * sha256 is the digest of the whole declaration, which the counts name. */
typedef struct BqRetirementRecordsDeclaration
{
    char* text;
    char* original;
    BqRetirementRowTemplate* templates;
    BqRetirementRecordsRule rules[BQ_RETIREMENT_RECORDS_RULES_CAP];
    BqRetirementRecordsTimed* timed;
    BqRetirementRecordsControl* controls;
    BqRetirementRecordsUntimed* untimed;
    u64 templates_offset, templates_length;
    u32 template_count, rule_count, timed_count, control_count, untimed_count;
    char sha256[SHA256_HEX_CAPACITY];
} BqRetirementRecordsDeclaration;

BUSTER_GLOBAL_LOCAL void bq_retirement_records_declaration_release(BqRetirementRecordsDeclaration* declaration)
{
    if (declaration)
    {
        free(declaration->text);
        free(declaration->original);
        free(declaration->templates);
        free(declaration->timed);
        free(declaration->controls);
        free(declaration->untimed);
        *declaration = (BqRetirementRecordsDeclaration){0};
    }
}

BUSTER_GLOBAL_LOCAL void bq_retirement_records_say(char* diagnostic, char const* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(diagnostic, BQ_RETIREMENT_RECORDS_DIAGNOSTIC, format, arguments);
    va_end(arguments);
}

/* The 1-based line the cursor last consumed. */
BUSTER_GLOBAL_LOCAL u32 bq_retirement_records_line(BqRetirementCheckCursor const* cursor)
{
    u32 line = 0;
    for (u64 index = 0; index < cursor->offset && index < cursor->bytes.length; index += 1)
        line += cursor->bytes.pointer[index] == '\n';
    return line ? line : 1u;
}

/* A stage name (the budget's stage order) as BQ_RETIREMENT_STAGE_*, else 0. */
BUSTER_GLOBAL_LOCAL u32 bq_retirement_records_stage(char const* text)
{
    u32 stage = 0;
    for (u32 index = 0; index < TP_RETIREMENT_BUDGET_STAGE_COUNT; index += 1)
        if (!strcmp(text, tp_retirement_budget_stage_names[index])) stage = BQ_RETIREMENT_STAGE_OBJECT + index;
    return stage;
}

/* A template index of kind, or "-" (none) when allowed. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_template(BqRetirementRecordsDeclaration const* declaration,
    char const* text, u32 kind, bool optional, u32* value)
{
    bool none = optional && !strcmp(text, "-");
    bool ok = none || (bq_retirement_check_decimal(text, declaration->template_count - 1u, value) &&
                       declaration->templates[*value].kind == kind);
    if (none) *value = BQ_RETIREMENT_ROW_PLAN_NONE;
    return ok;
}

BUSTER_GLOBAL_LOCAL u32 bq_retirement_records_rule_key(BqRetirementRecordsRule const* rule)
{
    u32 key = (rule->stage * 16u + rule->target) * 2u + (rule->runtime != BQ_RETIREMENT_ROW_PLAN_NONE);
    return key;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_records_declaration_rules(BqRetirementCheckCursor* cursor,
    BqRetirementRecordsDeclaration* declaration)
{
    u32 rules = bq_retirement_check_count(cursor, "compile-rules=", 0, BQ_RETIREMENT_RECORDS_RULES_CAP);
    for (u32 index = 0; cursor->ok && index < rules; index += 1)
    {
        BqRetirementRecordsRule* rule = declaration->rules + index;
        char* fields[4] = {0};
        char* value = bq_retirement_check_line(cursor, "compile=");
        rule->stage = cursor->ok && bq_retirement_check_fields(value, fields, 4) ? bq_retirement_records_stage(fields[0])
                                                                                 : 0;
        cursor->ok = cursor->ok && rule->stage && bq_retirement_check_decimal(fields[1], 12, &rule->target) &&
                     rule->target >= 1 &&
                     bq_retirement_records_template(declaration, fields[2], BQ_RETIREMENT_ROW_TEMPLATE_COMPILE, false,
                                                    &rule->compile) &&
                     bq_retirement_records_template(declaration, fields[3], BQ_RETIREMENT_ROW_TEMPLATE_RUNTIME, true,
                                                    &rule->runtime) &&
                     (!index || bq_retirement_records_rule_key(rule) > bq_retirement_records_rule_key(rule - 1));
    }
    declaration->rule_count = cursor->ok ? rules : 0;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_records_declaration_timed(BqRetirementCheckCursor* cursor,
    BqRetirementRecordsDeclaration* declaration)
{
    u32 groups = bq_retirement_check_count(cursor, "timed-groups=", 0, BQ_RETIREMENT_CORRECTNESS_ROWS_CAP);
    declaration->timed = cursor->ok ? calloc(groups + 1u, sizeof(*declaration->timed)) : NULL;
    cursor->ok = cursor->ok && declaration->timed;
    for (u32 index = 0; cursor->ok && index < groups; index += 1)
    {
        BqRetirementRecordsTimed* group = declaration->timed + index;
        char* fields[5] = {0};
        char* value = bq_retirement_check_line(cursor, "timed=");
        cursor->ok = cursor->ok && bq_retirement_check_fields(value, fields, 5) &&
                     bq_retirement_check_decimal(fields[0], BQ_RETIREMENT_CORRECTNESS_ROWS_CAP, &group->partition) &&
                     (!index || group->partition > group[-1].partition) &&
                     bq_retirement_records_template(declaration, fields[1], BQ_RETIREMENT_ROW_TEMPLATE_BATCH, false,
                                                    &group->template_index) &&
                     tp_retirement_metrics_word(fields[2], strlen(fields[2])) &&
                     bq_retirement_check_decimal(fields[3], 255, &group->exit_status) &&
                     bq_retirement_check_decimal(fields[4], TP_RETIREMENT_BATCH_INPUTS, &group->control_count);
        group->allocator = fields[2];
        group->first_control = declaration->control_count;
        BqRetirementRecordsControl* grown = cursor->ok && group->control_count ?
            realloc(declaration->controls, ((size_t)declaration->control_count + group->control_count) *
                                           sizeof(*declaration->controls)) : declaration->controls;
        cursor->ok = cursor->ok && (!group->control_count || grown);
        if (cursor->ok) declaration->controls = grown;
        for (u32 slot = 0; cursor->ok && slot < group->control_count; slot += 1)
        {
            BqRetirementRecordsControl* control = declaration->controls + declaration->control_count;
            char* control_fields[4] = {0};
            char* line = bq_retirement_check_line(cursor, "control=");
            bool named = false;
            cursor->ok = cursor->ok && bq_retirement_row_fields(line, control_fields, 4);
            if (cursor->ok)
            {
                named = strcmp(control_fields[0], "-") != 0;
                control->row = TP_RETIREMENT_BATCH_NO_ROW;
                control->status = control_fields[1];
                control->error = control_fields[2];
                control->fixture = named ? NULL : control_fields[3];
            }
            cursor->ok = cursor->ok &&
                         (!named || bq_retirement_check_decimal(control_fields[0], BQ_RETIREMENT_CORRECTNESS_ROWS_CAP - 1u,
                                                                &control->row)) &&
                         tp_retirement_metrics_status(control->status) && tp_retirement_metrics_error(control->error) &&
                         (named ? !strcmp(control_fields[3], "-") : tp_retirement_metrics_fixture(control->fixture));
            declaration->control_count += cursor->ok;
        }
    }
    declaration->timed_count = cursor->ok ? groups : 0;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_records_declaration_untimed(BqRetirementCheckCursor* cursor,
    BqRetirementRecordsDeclaration* declaration)
{
    u32 groups = bq_retirement_check_count(cursor, "untimed-groups=", 0, BQ_RETIREMENT_CORRECTNESS_ROWS_CAP);
    declaration->untimed = cursor->ok ? calloc(groups + 1u, sizeof(*declaration->untimed)) : NULL;
    cursor->ok = cursor->ok && declaration->untimed;
    for (u32 index = 0; cursor->ok && index < groups; index += 1)
    {
        BqRetirementRecordsUntimed* group = declaration->untimed + index;
        char* fields[4] = {0};
        char* value = bq_retirement_check_line(cursor, "untimed=");
        cursor->ok = cursor->ok && bq_retirement_check_fields(value, fields, 4) &&
                     bq_retirement_check_decimal(fields[0], BQ_RETIREMENT_CORRECTNESS_ROWS_CAP, &group->partition) &&
                     (!index || group->partition > group[-1].partition) &&
                     bq_retirement_records_template(declaration, fields[1], BQ_RETIREMENT_ROW_TEMPLATE_BATCH, false,
                                                    &group->template_index) &&
                     tp_retirement_metrics_word(fields[2], strlen(fields[2])) &&
                     tp_retirement_metrics_word(fields[3], strlen(fields[3]));
        group->target = fields[2];
        group->allocator = fields[3];
    }
    declaration->untimed_count = cursor->ok ? groups : 0;
}

/* The canonical declaration; anything else is refused with its line. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_declaration_parse(u8 const* bytes, u32 length,
    BqRetirementRecordsDeclaration* declaration, char* diagnostic)
{
    *declaration = (BqRetirementRecordsDeclaration){0};
    declaration->text = bytes && length ? malloc((size_t)length + 1u) : NULL;
    declaration->original = bytes && length ? malloc((size_t)length) : NULL;
    bool ok = declaration->text && declaration->original;
    if (ok)
    {
        memcpy(declaration->original, bytes, length);
        memcpy(declaration->text, bytes, length);
        bq_digest(bytes, length, (char8*)declaration->sha256);
        declaration->text[length] = 0;
        for (u32 index = 0; index < length; index += 1)
            if (declaration->text[index] == '\n') declaration->text[index] = 0;
    }
    BqRetirementCheckCursor cursor = {{(char8*)bytes, length}, declaration->text, 0,
                                      ok && memchr(bytes, 0, length) == NULL};
    String8 line = {0};
    cursor.ok = cursor.ok && bq_next_line(cursor.bytes, &cursor.offset, &line) &&
                string_equal(line, S8(BQ_RETIREMENT_RECORDS_DECLARATION_HEADER));
    declaration->templates_offset = cursor.offset;
    u32 templates = bq_retirement_check_count(&cursor, "templates=", 1, BQ_RETIREMENT_ROW_PLAN_TEMPLATES_CAP);
    declaration->templates = cursor.ok ? calloc(templates, sizeof(*declaration->templates)) : NULL;
    cursor.ok = cursor.ok && declaration->templates;
    for (u32 index = 0; cursor.ok && index < templates; index += 1)
        bq_retirement_row_template_decode(&cursor, declaration->templates + index, index);
    declaration->template_count = cursor.ok ? templates : 0;
    declaration->templates_length = cursor.offset - declaration->templates_offset;
    bq_retirement_records_declaration_rules(&cursor, declaration);
    bq_retirement_records_declaration_timed(&cursor, declaration);
    bq_retirement_records_declaration_untimed(&cursor, declaration);
    cursor.ok = cursor.ok && cursor.offset == cursor.bytes.length;
    if (!cursor.ok && ok)
        bq_retirement_records_say(diagnostic, "declaration line %u is not canonical %s",
                                  bq_retirement_records_line(&cursor), BQ_RETIREMENT_RECORDS_DECLARATION_HEADER);
    else if (!ok) bq_retirement_records_say(diagnostic, "empty declaration");
    ok = cursor.ok;
    if (!ok) bq_retirement_records_declaration_release(declaration);
    return ok;
}

/* Verbatim bytes into text (bq_retirement_row_text's growth and cap). */
BUSTER_GLOBAL_LOCAL void bq_retirement_records_raw(BqRetirementRowText* text, char const* bytes, u64 length)
{
    text->ok = text->ok && text->length + length <= BQ_RETIREMENT_ROW_PLAN_BYTES_CAP;
    if (text->ok && text->length + length + 1u > text->capacity)
    {
        u64 capacity = text->capacity ? text->capacity * 2u : 65536u;
        while (capacity < text->length + length + 1u) capacity *= 2u;
        char* grown = realloc(text->bytes, (size_t)capacity);
        text->ok = grown != NULL;
        if (grown)
        {
            text->bytes = grown;
            text->capacity = capacity;
        }
    }
    if (text->ok)
    {
        memcpy(text->bytes + text->length, bytes, (size_t)length);
        text->length += length;
        text->bytes[text->length] = 0;
    }
}

/* Row `row`'s fixture from the pinned performance rows, NUL-terminated. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_fixture(BqRetirementRecordsCensus const* census, u32 row,
    char fixture[BQ_RETIREMENT_RECORDS_FIXTURE_CAP])
{
    String8 value = bq_retirement_document_value(&census->population, row, BQ_RETIREMENT_DOCUMENT_FIXTURE);
    bool ok = value.length < BQ_RETIREMENT_RECORDS_FIXTURE_CAP;
    if (ok)
    {
        memcpy(fixture, value.pointer, (size_t)value.length);
        fixture[value.length] = 0;
    }
    ok = ok && tp_retirement_metrics_fixture(fixture);
    if (!ok) fixture[0] = 0;
    return ok;
}

/* The declared groups (partition indices, ascending) against the object
 * groups of partition: exactly one declared group per object group. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_cover(BqRetirementDocumentPartition const* partition,
    u32 const* declared, u32 stride, u32 count, char const* name, char* diagnostic)
{
    u32 next = 0;
    bool ok = true;
    for (u32 group = 0; ok && group < partition->count; group += 1)
    {
        bool listed = next < count && *(u32 const*)((u8 const*)declared + (size_t)next * stride) == group;
        u32 leader = partition->rows[partition->first[group]];
        ok = listed == (partition->object[group] != 0);
        if (!ok && listed)
            bq_retirement_records_say(diagnostic, "declared %s group %u is a singleton, not an object group", name, group);
        else if (!ok)
            bq_retirement_records_say(diagnostic, "%s object group %u (first row %u, %u rows) is not declared", name,
                                      group, leader, partition->first[group + 1] - partition->first[group]);
        next += listed;
    }
    if (ok && next != count)
    {
        ok = false;
        bq_retirement_records_say(diagnostic, "declared %s group %u is not in the %s partition (%u groups)", name,
                                  *(u32 const*)((u8 const*)declared + (size_t)next * stride), name, partition->count);
    }
    return ok;
}

/* The rule for a compiler-eligible row outside the timed batches. */
BUSTER_GLOBAL_LOCAL BqRetirementRecordsRule const* bq_retirement_records_rule(
    BqRetirementRecordsDeclaration const* declaration, BqRetirementTrustedRow const* row, bool runtime)
{
    BqRetirementRecordsRule const* found = NULL;
    for (u32 index = 0; !found && index < declaration->rule_count; index += 1)
    {
        BqRetirementRecordsRule const* rule = declaration->rules + index;
        if (rule->stage == row->stage && rule->target == row->target &&
            (rule->runtime != BQ_RETIREMENT_ROW_PLAN_NONE) == runtime)
            found = rule;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL char const* bq_retirement_records_stage_name(u32 stage)
{
    char const* name = stage >= BQ_RETIREMENT_STAGE_OBJECT && stage <= BQ_RETIREMENT_STAGE_SELF_HOST ?
                       tp_retirement_budget_stage_names[stage - BQ_RETIREMENT_STAGE_OBJECT] : "unknown";
    return name;
}

/* Marks each declared control's row with its group ordinal; a named control
 * must be a native, compiler-ineligible object row claimed once. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_controls(BqRetirementRecordsCensus const* census,
    BqRetirementRecordsDeclaration const* declaration, u32* marks, char* diagnostic)
{
    BqRetirementProjection const* projection = &census->projection;
    u32 native = projection->prepared.native_target;
    bool ok = true;
    for (u32 row = 0; row < projection->prepared.rows; row += 1) marks[row] = BQ_RETIREMENT_ROW_PLAN_NONE;
    for (u32 group = 0; ok && group < declaration->timed_count; group += 1)
    {
        BqRetirementRecordsTimed const* timed = declaration->timed + group;
        bool failing = false;
        for (u32 slot = 0; ok && slot < timed->control_count; slot += 1)
        {
            BqRetirementRecordsControl const* control = declaration->controls + timed->first_control + slot;
            u32 row = control->row;
            BqRetirementTrustedRow const* trusted = row != TP_RETIREMENT_BATCH_NO_ROW && row < projection->prepared.rows ?
                                                    projection->rows + row : NULL;
            failing = failing || strcmp(control->status, "ok") != 0;
            ok = row == TP_RETIREMENT_BATCH_NO_ROW ||
                 (trusted && !trusted->compiler_eligible && trusted->stage == BQ_RETIREMENT_STAGE_OBJECT &&
                  trusted->target == native && marks[row] == BQ_RETIREMENT_ROW_PLAN_NONE);
            if (ok && trusted) marks[row] = group;
            if (!ok)
                bq_retirement_records_say(diagnostic, "control row %u of timed group %u is not a native, "
                                          "compiler-ineligible object row claimed once", row, timed->partition);
        }
        if (ok && failing != (timed->exit_status != 0))
        {
            ok = false;
            bq_retirement_records_say(diagnostic, "timed group %u: exit status %u must be nonzero exactly when a "
                                      "control fails", timed->partition, timed->exit_status);
        }
    }
    return ok;
}

/* One timed group's group= and input= lines (group ordinal `ordinal`). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_group(BqRetirementRecordsCensus const* census,
    BqRetirementRecordsDeclaration const* declaration, TpRetirementCampaignBudget const* budget, u32 ordinal,
    BqRetirementRowText* text, char* diagnostic)
{
    BqRetirementRecordsTimed const* timed = declaration->timed + ordinal;
    BqRetirementDocumentPartition const* partition = &census->timed;
    u32 first = partition->first[timed->partition], members = partition->first[timed->partition + 1u] - first;
    u32 leader = partition->rows[first];
    u64 bound = 0;
    bool ok = tp_retirement_budget_metrics_bytes(budget, members + timed->control_count, &bound);
    if (!ok)
        bq_retirement_records_say(diagnostic, "timed group %u: %u inputs exceed the budget's metrics bound or %u inputs",
                                  timed->partition, members + timed->control_count, TP_RETIREMENT_BATCH_INPUTS);
    bq_retirement_row_text(text, "group=%u %u %s b%u.metrics %" PRIu64 " %u %u\n", ordinal, timed->template_index,
                           timed->allocator, leader, bound, timed->exit_status, members + timed->control_count);
    for (u32 member = 0; ok && member < members; member += 1)
    {
        u32 row = partition->rows[first + member];
        char fixture[BQ_RETIREMENT_RECORDS_FIXTURE_CAP];
        ok = bq_retirement_records_fixture(census, row, fixture);
        if (!ok) bq_retirement_records_say(diagnostic, "row %u has no valid fixture", row);
        bq_retirement_row_text(text, "input=%u 1 ok driver.none r%u.o %s\n", row, row, fixture);
    }
    for (u32 slot = 0; ok && slot < timed->control_count; slot += 1)
    {
        BqRetirementRecordsControl const* control = declaration->controls + timed->first_control + slot;
        bool named = control->row != TP_RETIREMENT_BATCH_NO_ROW;
        char fixture[BQ_RETIREMENT_RECORDS_FIXTURE_CAP], row[16] = "-", leaf[BQ_RETIREMENT_RECORDS_LEAF_CAP] = "-";
        ok = !named || bq_retirement_records_fixture(census, control->row, fixture);
        if (!ok) bq_retirement_records_say(diagnostic, "control row %u has no valid fixture", control->row);
        if (named) snprintf(row, sizeof(row), "%u", control->row);
        if (!strcmp(control->status, "ok") && named) snprintf(leaf, sizeof(leaf), "r%u.o", control->row);
        else if (!strcmp(control->status, "ok")) snprintf(leaf, sizeof(leaf), "c%u-%u.o", timed->partition, slot);
        bq_retirement_row_text(text, "input=%s 0 %s %s %s %s\n", row, control->status, control->error, leaf,
                               named ? fixture : control->fixture);
    }
    return ok;
}

/* The row plan for the census, the declaration, the reviewed budget's
 * metrics bounds and the host's CPU facts, checked by the importer's own
 * decode, bind and derive (plus distinct batch keys) before it is returned. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_row_plan(BqRetirementRecordsCensus const* census,
    BqRetirementRecordsDeclaration const* declaration, TpRetirementCampaignBudget const* budget,
    char const* cpu_model, u32 cpu, BqRetirementRowText* text, char* diagnostic)
{
    BqRetirementProjection const* projection = &census->projection;
    BqRetirementPrepared const* prepared = &projection->prepared;
    u32 native = prepared->native_target;
    u32* marks = calloc(prepared->rows ? prepared->rows : 1u, sizeof(*marks));
    *text = (BqRetirementRowText){.ok = true};
    bool ok = marks && cpu_model && bq_retirement_hex(string_from_pointer(cpu_model), 64) && cpu < CPU_SETSIZE &&
              tp_retirement_budget_valid(budget);
    if (!ok)
        bq_retirement_records_say(diagnostic, "the CPU model must be 64 lowercase hex, the CPU below %d and the "
                                  "budget valid", CPU_SETSIZE);
    ok = ok && bq_retirement_records_cover(&census->timed, &declaration->timed[0].partition,
                                           (u32)sizeof(*declaration->timed), declaration->timed_count, "timed",
                                           diagnostic) &&
         bq_retirement_records_controls(census, declaration, marks, diagnostic);
    bq_retirement_row_text(text, "BQ-RETIREMENT-ROW-PLAN-V1\nsupport=%s\ncensus=%s\npopulation=%s\nnative-target=%u\n"
                           "cpu=%s %u\n", prepared->support_sha256, prepared->census_sha256,
                           projection->population_sha256, native, cpu_model ? cpu_model : "", cpu);
    bq_retirement_records_raw(text, declaration->original + declaration->templates_offset,
                              declaration->templates_length);
    bq_retirement_row_text(text, "rows=%u\n", prepared->rows);
    for (u32 index = 0; ok && index < prepared->rows; index += 1)
    {
        BqRetirementTrustedRow const* row = projection->rows + index;
        bool batch = bq_retirement_row_timed_object(row, native) || marks[index] != BQ_RETIREMENT_ROW_PLAN_NONE;
        bool runtime = bq_retirement_row_native_runtime(row, native);
        BqRetirementRecordsRule const* rule = !batch && row->compiler_eligible ?
                                              bq_retirement_records_rule(declaration, row, runtime) : NULL;
        char fixture[BQ_RETIREMENT_RECORDS_FIXTURE_CAP] = "-", compile[16] = "-", run[16] = "-";
        ok = batch || !row->compiler_eligible || rule;
        if (!ok)
            bq_retirement_records_say(diagnostic, "row %u (%s, target %u%s) has no compile rule", index,
                                      bq_retirement_records_stage_name(row->stage), row->target,
                                      runtime ? ", native runtime" : "");
        bool compiled = batch || rule;
        if (ok && compiled && !bq_retirement_records_fixture(census, index, fixture))
        {
            ok = false;
            bq_retirement_records_say(diagnostic, "row %u has no valid fixture", index);
        }
        if (batch) snprintf(compile, sizeof(compile), "batch");
        else if (rule) snprintf(compile, sizeof(compile), "%u", rule->compile);
        if (rule && rule->runtime != BQ_RETIREMENT_ROW_PLAN_NONE) snprintf(run, sizeof(run), "%u", rule->runtime);
        bq_retirement_row_text(text, "row=%u %s %s %s\n", index, compile, run, fixture);
    }
    bq_retirement_row_text(text, "groups=%u\n", declaration->timed_count);
    for (u32 group = 0; ok && group < declaration->timed_count; group += 1)
        ok = bq_retirement_records_group(census, declaration, budget, group, text, diagnostic);
    if (ok && !text->ok)
    {
        ok = false;
        bq_retirement_records_say(diagnostic, "the plan exceeds %u bytes or a line's bound",
                                  BQ_RETIREMENT_ROW_PLAN_BYTES_CAP);
    }
    BqRetirementRowPlan plan = {0};
    if (ok && bq_retirement_row_plan_parse((u8 const*)text->bytes, (u32)text->length, projection, &plan) != BQ_OK)
    {
        ok = false;
        bq_retirement_records_say(diagnostic, "the generated plan does not import (a control's status and error "
                                  "disagree, a template breaks the A1 contract, or the population seal fails)");
    }
    /* No two groups share a batch key: sorted, as the correctness gate
     * checks its frozen groups (bq_retirement_correctness_key_order). */
    char const** keys = ok && plan.group_count > 1 ? malloc((size_t)plan.group_count * sizeof(*keys)) : NULL;
    if (ok && plan.group_count > 1 && !keys)
    {
        ok = false;
        bq_retirement_records_say(diagnostic, "out of memory");
    }
    for (u32 group = 0; keys && group < plan.group_count; group += 1) keys[group] = plan.groups[group].key_sha256;
    if (keys) qsort(keys, plan.group_count, sizeof(*keys), bq_retirement_correctness_key_order);
    for (u32 index = 1; ok && keys && index < plan.group_count; index += 1)
    {
        ok = strcmp(keys[index - 1u], keys[index]) != 0;
        if (!ok)
        {
            size_t stride = sizeof(*plan.groups);
            u32 left = (u32)((size_t)((char const*)keys[index - 1u] - plan.groups[0].key_sha256) / stride);
            u32 right = (u32)((size_t)((char const*)keys[index] - plan.groups[0].key_sha256) / stride);
            bq_retirement_records_say(diagnostic, "timed groups %u and %u share a batch key (template and allocator)",
                                      declaration->timed[left < right ? left : right].partition,
                                      declaration->timed[left < right ? right : left].partition);
        }
    }
    free(keys);
    bq_retirement_row_plan_release(&plan);
    free(marks);
    if (!ok)
    {
        free(text->bytes);
        *text = (BqRetirementRowText){0};
    }
    return ok;
}

/* The untimed-command contract for a row plan generated from this census
 * and declaration (its bytes: the contract binds their digest), checked by
 * the campaign's own parser before it is returned. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_untimed(BqRetirementRecordsCensus const* census,
    BqRetirementRecordsDeclaration const* declaration, u8 const* plan_bytes, u32 plan_length, BqRetirementRowText* text,
    char* diagnostic)
{
    BqRetirementDocumentPartition const* partition = &census->untimed;
    BqRetirementRowPlan plan = {0};
    *text = (BqRetirementRowText){.ok = true};
    bool ok = plan_length <= BQ_RETIREMENT_ROW_PLAN_BYTES_CAP &&
              bq_retirement_row_plan_parse(plan_bytes, plan_length, &census->projection, &plan) == BQ_OK;
    if (!ok)
        bq_retirement_records_say(diagnostic, "the row plan does not import against this census (wrong support, "
                                  "census or population digest, rows or groups)");
    if (ok) bq_digest((char8 const*)plan_bytes, plan_length, (char8*)plan.authority_sha256);
    if (ok) plan.owned = 1;
    ok = ok && plan.template_count == declaration->template_count;
    for (u32 index = 0; ok && index < plan.template_count; index += 1)
        ok = !memcmp(plan.templates[index].template_sha256, declaration->templates[index].template_sha256,
                     SHA256_HEX_CAPACITY);
    if (!ok && plan.owned)
        bq_retirement_records_say(diagnostic, "the row plan's templates are not this declaration's");
    ok = ok && bq_retirement_records_cover(partition, &declaration->untimed[0].partition,
                                           (u32)sizeof(*declaration->untimed), declaration->untimed_count, "untimed",
                                           diagnostic);
    bq_retirement_row_text(text, BQ_RETIREMENT_WORKER_UNTIMED_HEADER "\nrow-plan=%s\ngroups=%u\n", plan.authority_sha256,
                           declaration->untimed_count);
    for (u32 index = 0; ok && index < declaration->untimed_count; index += 1)
    {
        BqRetirementRecordsUntimed const* group = declaration->untimed + index;
        u32 first = partition->first[group->partition], members = partition->first[group->partition + 1u] - first;
        bq_retirement_row_text(text, "group=%u %u %s %s u%u.metrics %u\n", group->partition, group->template_index,
                               group->target, group->allocator, partition->rows[first], members);
        for (u32 member = 0; member < members; member += 1)
            bq_retirement_row_text(text, "input=%u r%u.o\n", partition->rows[first + member],
                                   partition->rows[first + member]);
    }
    if (ok && !(text->ok && text->length <= BQ_RETIREMENT_WORKER_UNTIMED_BYTES_CAP))
    {
        ok = false;
        bq_retirement_records_say(diagnostic, "the contract exceeds %u bytes", BQ_RETIREMENT_WORKER_UNTIMED_BYTES_CAP);
    }
    Arena* arena = ok ? arena_create((ArenaCreation){.reserved_size = UINT64_C(1) << 32, .flags = {.no_pool = 1}}) : NULL;
    BqRetirementWorkerUntimedContract contract = {0};
    if (ok && !(arena && bq_retirement_worker_untimed_parse(arena, (u8 const*)text->bytes, (u32)text->length, &plan,
                                                            partition, &contract) == BQ_OK))
    {
        ok = false;
        bq_retirement_records_say(diagnostic, "the generated contract does not import (a group exceeds %u members)",
                                  TP_RETIREMENT_BATCH_INPUTS);
    }
    if (arena) arena_destroy(arena, 1);
    bq_retirement_row_plan_release(&plan);
    if (!ok)
    {
        free(text->bytes);
        *text = (BqRetirementRowText){0};
    }
    return ok;
}

/* The frozen counts of the campaign, naming the census population seal and
 * the declaration's digest: the timed partition in campaign order (each
 * object group's members plus its declared controls, each singleton by its
 * link or self-host stage), the runtime-eligible timed rows, the frozen pair
 * count and the untimed partition. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_counts(BqRetirementRecordsCensus const* census,
    BqRetirementRecordsDeclaration const* declaration, u32 pairs, BqRetirementRowText* text, char* diagnostic)
{
    BqRetirementProjection const* projection = &census->projection;
    u32 native = projection->prepared.native_target;
    u32 groups = census->timed.count + census->untimed.count;
    unsigned* storage = calloc(3u * (groups ? groups : 1u), sizeof(*storage));
    *text = (BqRetirementRowText){0};
    bool ok = storage && bq_retirement_records_cover(&census->timed, &declaration->timed[0].partition,
                                                     (u32)sizeof(*declaration->timed), declaration->timed_count,
                                                     "timed", diagnostic);
    unsigned* inputs = storage;
    unsigned* kinds = storage ? storage + groups : NULL;
    unsigned* stages = storage ? storage + 2u * groups : NULL;
    unsigned runtime = 0;
    for (u32 row = 0; ok && row < projection->prepared.rows; row += 1)
        runtime += bq_retirement_row_native_runtime(projection->rows + row, native);
    for (u32 list = 0, used = 0, declared = 0; ok && list < 2; list += 1)
    {
        BqRetirementDocumentPartition const* partition = list ? &census->untimed : &census->timed;
        for (u32 group = 0; group < partition->count; group += 1, used += 1)
        {
            u32 first = partition->first[group], leader = partition->rows[first];
            bool object = partition->object[group] != 0;
            u32 stage = projection->rows[leader].stage;
            inputs[used] = partition->first[group + 1u] - first;
            if (object && !list) inputs[used] += declaration->timed[declared++].control_count;
            kinds[used] = object ? TP_RETIREMENT_GROUP_OBJECT : TP_RETIREMENT_GROUP_SINGLETON;
            stages[used] = object ? TP_RETIREMENT_BUDGET_STAGE_OBJECT :
                           stage == BQ_RETIREMENT_STAGE_SELF_HOST ? TP_RETIREMENT_BUDGET_STAGE_SELF_HOST :
                           stage == BQ_RETIREMENT_STAGE_LINK ? TP_RETIREMENT_BUDGET_STAGE_LINK :
                           TP_RETIREMENT_BUDGET_STAGE_COUNT;
            /* A singleton is a link or self-host row, as the layout builder
             * and bq_retirement_worker_untimed_build require. */
            if (ok && stages[used] == TP_RETIREMENT_BUDGET_STAGE_COUNT)
            {
                ok = false;
                bq_retirement_records_say(diagnostic, "%s singleton group %u (row %u) is neither a link nor a "
                                          "self-host row", list ? "untimed" : "timed", group, leader);
            }
        }
    }
    u32 timed = census->timed.count;
    TpRetirementBudgetCounts counts = {{inputs, kinds, stages, timed},
        {inputs ? inputs + timed : NULL, kinds ? kinds + timed : NULL, stages ? stages + timed : NULL,
         census->untimed.count}, runtime, pairs};
    char const* population = projection->population_sha256;
    size_t size = ok ? tp_retirement_budget_counts_encode(&counts, population, declaration->sha256, NULL, 0) : 0;
    if (ok && !size)
        bq_retirement_records_say(diagnostic, "a group exceeds %u inputs", TP_RETIREMENT_BATCH_INPUTS);
    text->bytes = size ? malloc(size + 1u) : NULL;
    ok = ok && text->bytes &&
         tp_retirement_budget_counts_encode(&counts, population, declaration->sha256, text->bytes, size) == size;
    if (ok)
    {
        text->bytes[size] = 0;
        text->length = size;
        text->capacity = size + 1u;
        text->ok = true;
    }
    else
    {
        free(text->bytes);
        *text = (BqRetirementRowText){0};
    }
    free(storage);
    return ok;
}

/* A whole regular file (at most cap bytes) into a heap copy; a final
 * symlink, a FIFO or any other non-regular file is refused without
 * blocking. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_read(char const* path, u32 cap, u8** bytes, u32* length)
{
    int file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat info = {0};
    bool ok = file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
              (u64)info.st_size <= cap;
    *bytes = ok ? malloc((size_t)info.st_size) : NULL;
    ok = ok && *bytes && bq_read(file, *bytes, (u32)info.st_size, 0);
    *length = ok ? (u32)info.st_size : 0;
    if (!ok)
    {
        free(*bytes);
        *bytes = NULL;
    }
    if (file >= 0) close(file);
    return ok;
}

/* The profile's frozen pair count (campaign-pairs=), under the rules
 * bq_retirement_unit_campaign_pins applies: present once, canonical, even,
 * within 60..254. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_pairs(String8 profile, u32* pairs)
{
    String8 value = {0};
    uint64_t number = 0;
    bool ok = bq_retirement_unit_campaign_profile_value(profile, BQ_RETIREMENT_UNIT_CAMPAIGN_PAIRS_KEY, &value) &&
              bq_retirement_unit_campaign_decimal(value, &number) && number >= TP_RETIREMENT_MIN_PAIRS_PER_ROUND &&
              number <= TP_RETIREMENT_EXECUTION_MAX_PAIRS && !(number & 1);
    *pairs = ok ? (u32)number : 0;
    return ok;
}

/* Writes bytes to path exclusively: a new temporary sibling
 * (O_CREAT | O_EXCL, mode 0444, fsynced) linked to path with linkat, which
 * never replaces an existing name, then the temporary unlinked and the
 * directory fsynced. "-" is output. Nothing is left behind on failure. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_records_publish(char const* path, char const* bytes, u64 length, FILE* output,
    char* diagnostic)
{
    bool stream = !strcmp(path, "-");
    char directory[BQ_RETIREMENT_RECORDS_PATH_CAP], temporary[BQ_RETIREMENT_RECORDS_PATH_CAP];
    char const* slash = strrchr(path, '/');
    char const* leaf = slash ? slash + 1 : path;
    size_t prefix = slash ? (size_t)(slash - path) : 0;
    bool ok = stream || (leaf[0] && strcmp(leaf, ".") && strcmp(leaf, "..") && prefix < sizeof(directory) &&
                         strlen(leaf) + 32u < sizeof(temporary));
    if (ok && !stream)
    {
        memcpy(directory, slash ? path : ".", slash ? prefix : 1u);
        directory[slash ? prefix : 1u] = 0;
        if (slash && !prefix) snprintf(directory, sizeof(directory), "/");
        snprintf(temporary, sizeof(temporary), ".%s.tmp-%ld", leaf, (long)getpid());
    }
    int parent = ok && !stream ? open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    int file = parent >= 0 ? openat(parent, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0444) : -1;
    bool created = file >= 0;
    if (stream) ok = fwrite(bytes, 1, (size_t)length, output) == length && fflush(output) == 0;
    else
    {
        ok = created && length <= UINT32_MAX && bq_write_all(file, (u8 const*)bytes, (u32)length) && fsync(file) == 0;
        if (file >= 0 && close(file) != 0) ok = false;
        ok = ok && linkat(parent, temporary, parent, leaf, 0) == 0;
        if (created && unlinkat(parent, temporary, 0) != 0) ok = false;
        ok = ok && fsync(parent) == 0;
    }
    if (parent >= 0) close(parent);
    if (!ok)
        bq_retirement_records_say(diagnostic, "cannot write %s (it must not exist yet; its directory must be "
                                  "writable)", path);
    return ok;
}

/* The generators:
 *   row-plan INSTALLED PROFILE DECLARATION BUDGET CPU_MODEL_SHA256 CPU OUTPUT
 *   untimed-commands INSTALLED PROFILE DECLARATION ROW_PLAN OUTPUT
 *   budget-counts INSTALLED PROFILE DECLARATION PAIRS OUTPUT
 *   budget-preflight INSTALLED PROFILE DECLARATION BUDGET COUNTS
 * Each record goes to OUTPUT exclusively (bq_retirement_records_publish;
 * "-" is output); budget-preflight regenerates the counts, requires COUNTS to
 * be exactly them (so stale counts, of another census, declaration or pair
 * count, refuse) and reports the budget's derivation on output. INSTALLED is
 * the absolute staged installed root whose recipes/ holds the census the
 * profile's pins name (read-only files, as the service requires);
 * census_profile is the validator profile its report must declare; PAIRS
 * must equal the profile's campaign-pairs=. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_records_generate(int argc, char** argv, String8 census_profile,
    FILE* output, FILE* diagnostics)
{
    char diagnostic[BQ_RETIREMENT_RECORDS_DIAGNOSTIC] = "usage: retirement-records row-plan INSTALLED PROFILE "
        "DECLARATION BUDGET CPU_MODEL_SHA256 CPU OUTPUT | untimed-commands INSTALLED PROFILE DECLARATION ROW_PLAN "
        "OUTPUT | budget-counts INSTALLED PROFILE DECLARATION PAIRS OUTPUT | budget-encode REVIEWED_INPUT COUNTS "
        "OUTPUT | budget-preflight INSTALLED PROFILE DECLARATION BUDGET COUNTS";
    bool plan = argc == 8 && !strcmp(argv[0], "row-plan");
    bool untimed = argc == 6 && !strcmp(argv[0], "untimed-commands");
    bool counts = argc == 6 && !strcmp(argv[0], "budget-counts");
    bool preflight = argc == 6 && !strcmp(argv[0], "budget-preflight");
    BqError result = (plan || untimed || counts || preflight) && argv[1][0] == '/' ? BQ_OK : BQ_BAD_REQUEST;
    u8* profile = NULL;
    u8* declared = NULL;
    u8* extra = NULL;
    u8* frozen = NULL;
    u32 profile_length = 0, declared_length = 0, extra_length = 0, frozen_length = 0, number = 0, pairs = 0;
    if (result == BQ_OK &&
        !(bq_retirement_records_read(argv[2], BQ_RETIREMENT_RECORDS_PROFILE_BYTES_CAP, &profile, &profile_length) &&
          bq_retirement_records_read(argv[3], BQ_RETIREMENT_RECORDS_DECLARATION_BYTES_CAP, &declared, &declared_length) &&
          (counts || bq_retirement_records_read(argv[4], plan || preflight ? TP_RETIREMENT_BUDGET_BYTES - 1u :
                                                BQ_RETIREMENT_ROW_PLAN_BYTES_CAP, &extra, &extra_length)) &&
          (!preflight || bq_retirement_records_read(argv[5], TP_RETIREMENT_BUDGET_COUNTS_BYTES, &frozen,
                                                    &frozen_length))))
    {
        result = BQ_IO;
        bq_retirement_records_say(diagnostic, "cannot read an input file (missing, not a regular file, empty or too "
                                  "large)");
    }
    String8 profile_text = {(char8*)profile, profile_length};
    if (result == BQ_OK && ((plan && !bq_retirement_check_decimal(argv[6], CPU_SETSIZE - 1, &number)) ||
                            (counts && !bq_retirement_check_decimal(argv[4], UINT32_MAX, &number))))
    {
        result = BQ_BAD_REQUEST;
        bq_retirement_records_say(diagnostic, "%s is not a canonical decimal in range", plan ? argv[6] : argv[4]);
    }
    if (result == BQ_OK && (counts || preflight) &&
        !(bq_retirement_records_pairs(profile_text, &pairs) && (!counts || number == pairs)))
    {
        result = BQ_RECIPE_MISMATCH;
        bq_retirement_records_say(diagnostic, "the profile's campaign-pairs= is missing, not an even count in "
                                  "%u..%u, or not the given pair count", TP_RETIREMENT_MIN_PAIRS_PER_ROUND,
                                  TP_RETIREMENT_EXECUTION_MAX_PAIRS);
    }
    TpRetirementCampaignBudget budget = {0};
    if (result == BQ_OK && plan && !tp_retirement_budget_decode((char const*)extra, extra_length, &budget))
    {
        result = BQ_RECIPE_MISMATCH;
        bq_retirement_records_say(diagnostic, "%s is not a canonical %s record", argv[4], TP_RETIREMENT_BUDGET_SCHEMA);
    }
    BqRetirementRecordsDeclaration declaration = {0};
    if (result == BQ_OK && !bq_retirement_records_declaration_parse(declared, declared_length, &declaration, diagnostic))
        result = BQ_RECIPE_MISMATCH;
    int installed = result == BQ_OK ? bq_open_absolute_directory(string_from_pointer(argv[1])) : -1;
    BqRetirementRecordsCensus census = {0};
    if (result == BQ_OK)
    {
        result = bq_retirement_records_census(installed, profile_text, census_profile, &census);
        if (result != BQ_OK)
            bq_retirement_records_say(diagnostic, "the census under %s/recipes does not match the profile's pins or "
                                      "does not project (%s)", argv[1], bq_error_name(result));
    }
    BqRetirementRowText text = {0};
    bool made = result == BQ_OK &&
                (plan ? bq_retirement_records_row_plan(&census, &declaration, &budget, argv[5], number, &text, diagnostic) :
                 untimed ? bq_retirement_records_untimed(&census, &declaration, extra, extra_length, &text, diagnostic) :
                 bq_retirement_records_counts(&census, &declaration, pairs, &text, diagnostic));
    if (result == BQ_OK && !made) result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && preflight && !(frozen_length == text.length && !memcmp(frozen, text.bytes, frozen_length)))
    {
        result = BQ_RECIPE_MISMATCH;
        bq_retirement_records_say(diagnostic, "%s are not the counts of this census, declaration and pair count "
                                  "(regenerate them with budget-counts)", argv[5]);
    }
    char canonical[TP_RETIREMENT_BUDGET_BYTES], digest[SHA256_HEX_CAPACITY] = {0};
    size_t canonical_size = 0;
    TpRetirementBudgetPreflight derived = {0};
    if (result == BQ_OK && preflight &&
        !(tp_retirement_budget_evaluate(0, (char const*)extra, extra_length, text.bytes, (size_t)text.length, canonical,
                                        &canonical_size, digest, &derived, diagnostic) &&
          tp_retirement_budget_report(output, digest, &derived) && fflush(output) == 0))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !preflight &&
        !bq_retirement_records_publish(argv[argc - 1], text.bytes, text.length, output, diagnostic))
        result = BQ_IO;
    if (result != BQ_OK) fprintf(diagnostics, "retirement-records: %s\n", diagnostic);
    free(text.bytes);
    bq_retirement_records_census_release(&census);
    if (installed >= 0) close(installed);
    bq_retirement_records_declaration_release(&declaration);
    free(profile);
    free(declared);
    free(extra);
    free(frozen);
    return result;
}

/* `budget-encode REVIEWED_INPUT COUNTS OUTPUT`: lane D's budget writer
 * (tp_retirement_budget_evaluate, retirement_budget_tool.h) with the record
 * published exclusively. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_records_encode(char** argv, FILE* output, FILE* diagnostics)
{
    char diagnostic[BQ_RETIREMENT_RECORDS_DIAGNOSTIC] = {0};
    u8* input = NULL;
    u8* counts = NULL;
    u32 input_length = 0, counts_length = 0;
    char canonical[TP_RETIREMENT_BUDGET_BYTES], digest[SHA256_HEX_CAPACITY] = {0};
    size_t size = 0;
    TpRetirementBudgetPreflight derived = {0};
    BqError result = bq_retirement_records_read(argv[1], TP_RETIREMENT_BUDGET_INPUT_BYTES, &input, &input_length) &&
                     bq_retirement_records_read(argv[2], TP_RETIREMENT_BUDGET_COUNTS_BYTES, &counts, &counts_length) ?
                     BQ_OK : BQ_IO;
    if (result != BQ_OK) tp_retirement_budget_say(diagnostic, "cannot read %s or %s", argv[1], argv[2]);
    if (result == BQ_OK && !tp_retirement_budget_evaluate(1, (char const*)input, input_length, (char const*)counts,
                                                          counts_length, canonical, &size, digest, &derived, diagnostic))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_records_publish(argv[3], canonical, size, output, diagnostic)) result = BQ_IO;
    if (result != BQ_OK) fprintf(diagnostics, "retirement-budget: %s\n", diagnostic);
    free(input);
    free(counts);
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_records_run(int argc, char** argv, String8 census_profile, FILE* output,
    FILE* diagnostics)
{
    bool encode = argc == 4 && !strcmp(argv[0], "budget-encode");
    BqError result = encode ? bq_retirement_records_encode(argv, output, diagnostics) :
                     bq_retirement_records_generate(argc, argv, census_profile, output, diagnostics);
    return result;
}

/* The production entry: the report must declare the full census. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_records_cli(int argc, char** argv, FILE* output, FILE* diagnostics)
{
    BqError result = bq_retirement_records_run(argc, argv, S8("full-census"), output, diagnostics);
    return result;
}
