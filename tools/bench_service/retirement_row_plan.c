/* Row-plan authority, its derivation, the canonical row evidence and the
 * join (#1020/#509 design step 9, #881 lane B).
 *
 * Ownership: the per-row half of step 9's authority. The pinned file is read
 * and bound to one attempt's sealed projection here; the producer that runs
 * it lives in retirement_row_producer.c and the gate that admits the joined
 * evidence in retirement_unit.c.
 *
 * Entry points:
 *   bq_retirement_row_plan_import       the pinned plan for one attempt
 *   bq_retirement_row_plan_parse        its decode, bind and derive of the
 *                                       bytes, also run by the offline
 *                                       generator (retirement_records.c)
 *   bq_retirement_row_observed_format   canonical row-evidence bytes
 *   bq_retirement_row_observed_parse    and back, canonical only
 *   bq_retirement_row_evidence_join     plan + observation -> gate evidence
 *
 * Map: bq_retirement_row_token and bq_retirement_row_field check one
 * template field; bq_retirement_row_resolve and bq_retirement_row_command
 * resolve a template in the canonical child layout, and
 * bq_retirement_row_command_digest hashes it as the campaign does;
 * bq_retirement_row_template_decode, bq_retirement_row_plan_decode_rows and
 * bq_retirement_row_plan_decode_groups read the canonical text;
 * bq_retirement_row_plan_bind checks the plan against the projection and the
 * A1 contract; bq_retirement_row_plan_derive fills the batch keys, command
 * digests, completed rows and the second A/A label aggregate;
 * BqRetirementRowText is the growable formatter.
 */
#include "retirement_row_plan.h"
#include <stdarg.h>

#define BQ_RETIREMENT_ROW_SLOT_BYTES (BQ_RETIREMENT_CHECK_FIELD_CAP + 1024u)

typedef enum BqRetirementRowToken
{
    BQ_RETIREMENT_ROW_TOKEN_NONE,
    BQ_RETIREMENT_ROW_TOKEN_BINARY,
    BQ_RETIREMENT_ROW_TOKEN_SOURCE_BASE,
    BQ_RETIREMENT_ROW_TOKEN_SOURCE_CANDIDATE,
    BQ_RETIREMENT_ROW_TOKEN_WORK,
    BQ_RETIREMENT_ROW_TOKEN_FIXTURE,
    BQ_RETIREMENT_ROW_TOKEN_OUTPUT,
    BQ_RETIREMENT_ROW_TOKEN_METRICS,
    BQ_RETIREMENT_ROW_TOKEN_INPUTS,
    BQ_RETIREMENT_ROW_TOKEN_LABEL,
    BQ_RETIREMENT_ROW_TOKEN_COUNT
} BqRetirementRowToken;

BUSTER_GLOBAL_LOCAL char const* const bq_retirement_row_token_names[BQ_RETIREMENT_ROW_TOKEN_COUNT] = {
    "", "{{binary}}", "{{source:0}}", "{{source:1}}", "{{work}}", "{{fixture}}", "{{output}}", "{{metrics}}",
    "{{inputs}}", "{{label}}"
};
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_row_kind_names[BQ_RETIREMENT_ROW_TEMPLATE_COUNT] = {
    "", "compile", "batch", "runtime"
};

#define BQ_RETIREMENT_ROW_BIT(token) (1u << (token))
#define BQ_RETIREMENT_ROW_SHARED_TOKENS (BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_SOURCE_BASE) | \
    BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_SOURCE_CANDIDATE) | BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_WORK) | \
    BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_LABEL))

/* The tokens each template kind may use, and those it must use. */
BUSTER_GLOBAL_LOCAL u32 const bq_retirement_row_allowed_tokens[BQ_RETIREMENT_ROW_TEMPLATE_COUNT] = {
    0,
    BQ_RETIREMENT_ROW_SHARED_TOKENS | BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_BINARY) |
        BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_FIXTURE) | BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_OUTPUT) |
        BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_METRICS),
    BQ_RETIREMENT_ROW_SHARED_TOKENS | BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_BINARY) |
        BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_METRICS) | BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_INPUTS),
    BQ_RETIREMENT_ROW_SHARED_TOKENS | BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_OUTPUT)
};
BUSTER_GLOBAL_LOCAL u32 const bq_retirement_row_required_tokens[BQ_RETIREMENT_ROW_TEMPLATE_COUNT] = {
    0,
    BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_FIXTURE) | BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_OUTPUT) |
        BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_METRICS),
    BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_METRICS) | BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_INPUTS),
    BQ_RETIREMENT_ROW_BIT(BQ_RETIREMENT_ROW_TOKEN_OUTPUT)
};

/* The token starting at text, if any: its length and kind. */
BUSTER_GLOBAL_LOCAL u32 bq_retirement_row_token(char const* text, u32* token)
{
    u32 length = 0;
    *token = BQ_RETIREMENT_ROW_TOKEN_NONE;
    for (u32 candidate = 1; !length && candidate < BQ_RETIREMENT_ROW_TOKEN_COUNT; candidate += 1)
    {
        size_t size = strlen(bq_retirement_row_token_names[candidate]);
        if (!strncmp(text, bq_retirement_row_token_names[candidate], size))
        {
            length = (u32)size;
            *token = candidate;
        }
    }
    return length;
}

/* A printable-ASCII field of at most FIELD_CAP bytes (empty only when
 * allowed) whose "{{" only ever opens a token the kind allows; seen collects
 * the tokens it uses. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_field(char const* text, u32 kind, bool allow_empty, u32* seen)
{
    size_t length = text ? strnlen(text, BQ_RETIREMENT_CHECK_FIELD_CAP + 1u) : 0;
    bool ok = text && length <= BQ_RETIREMENT_CHECK_FIELD_CAP && (allow_empty || length > 0);
    size_t offset = 0;
    while (ok && offset < length)
    {
        u8 byte = (u8)text[offset];
        bool opens = byte == '{' && text[offset + 1] == '{';
        u32 token = 0;
        u32 size = opens ? bq_retirement_row_token(text + offset, &token) : 0;
        ok = byte >= 0x20 && byte <= 0x7e && (!opens || (size && (bq_retirement_row_allowed_tokens[kind] &
                                                                   BQ_RETIREMENT_ROW_BIT(token))));
        if (ok && size) *seen |= BQ_RETIREMENT_ROW_BIT(token);
        offset += size ? size : 1u;
    }
    return ok;
}

/* What the tokens of one step resolve to. */
typedef struct BqRetirementRowContext
{
    char const* fixture;
    char const* output;
    char const* metrics;
    char const* inputs;
    u32 side, label;
} BqRetirementRowContext;

/* One template field with every token replaced, into output. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_resolve(char const* template, BqRetirementRowContext const* context,
    char* output, u32 capacity)
{
    char descriptor[3][24];
    snprintf(descriptor[0], sizeof(descriptor[0]), "/proc/self/fd/%d", BQ_RETIREMENT_ROW_SLOT_BINARY + (int)context->side);
    snprintf(descriptor[1], sizeof(descriptor[1]), "/proc/self/fd/%d", BQ_RETIREMENT_ROW_SLOT_SOURCE);
    snprintf(descriptor[2], sizeof(descriptor[2]), "/proc/self/fd/%d", BQ_RETIREMENT_ROW_SLOT_SOURCE + 1);
    char const* values[BQ_RETIREMENT_ROW_TOKEN_COUNT] = {NULL, descriptor[0], descriptor[1], descriptor[2],
        BQ_RETIREMENT_ROW_WORK_PATH, context->fixture, context->output, context->metrics, context->inputs,
        context->label == 2 ? "2" : "1"};
    u32 used = 0;
    bool ok = template && output && capacity;
    for (size_t offset = 0; ok && template[offset];)
    {
        u32 token = 0;
        u32 size = template[offset] == '{' ? bq_retirement_row_token(template + offset, &token) : 0;
        char const* value = size ? values[token] : NULL;
        size_t length = size ? (value ? strlen(value) : 0) : 1u;
        ok = (!size || value) && used + length < capacity;
        if (ok)
        {
            memcpy(output + used, size ? value : template + offset, length);
            used += (u32)length;
        }
        offset += size ? size : 1u;
    }
    if (ok) output[used] = 0;
    return ok;
}

/* A resolved command. storage holds every slot and belongs to the caller. */
typedef struct BqRetirementRowCommand
{
    char* arguments[BQ_RETIREMENT_CHECK_ARGUMENTS_CAP + 1];
    char* environment[BQ_RETIREMENT_CHECK_ENVIRONMENT_CAP + 1];
    char* storage;
    u32 argument_count, environment_count;
} BqRetirementRowCommand;

#define BQ_RETIREMENT_ROW_COMMAND_STORAGE \
    ((size_t)(BQ_RETIREMENT_CHECK_ARGUMENTS_CAP + BQ_RETIREMENT_CHECK_ENVIRONMENT_CAP) * BQ_RETIREMENT_ROW_SLOT_BYTES)

BUSTER_GLOBAL_LOCAL bool bq_retirement_row_command(BqRetirementRowTemplate const* template,
    BqRetirementRowContext const* context, char* storage, BqRetirementRowCommand* command)
{
    *command = (BqRetirementRowCommand){.storage = storage, .argument_count = template->argument_count,
                                        .environment_count = template->environment_count};
    bool ok = storage != NULL;
    for (u32 index = 0; ok && index < template->argument_count; index += 1)
    {
        command->arguments[index] = storage + (size_t)index * BQ_RETIREMENT_ROW_SLOT_BYTES;
        ok = bq_retirement_row_resolve(template->arguments[index], context, command->arguments[index],
                                       BQ_RETIREMENT_ROW_SLOT_BYTES);
    }
    for (u32 index = 0; ok && index < template->environment_count; index += 1)
    {
        command->environment[index] = storage + (size_t)(BQ_RETIREMENT_CHECK_ARGUMENTS_CAP + index) *
                                                BQ_RETIREMENT_ROW_SLOT_BYTES;
        ok = bq_retirement_row_resolve(template->environment[index], context, command->environment[index],
                                       BQ_RETIREMENT_ROW_SLOT_BYTES);
    }
    command->arguments[template->argument_count] = NULL;
    command->environment[template->environment_count] = NULL;
    return ok;
}

/* The canonical command identity (argv, the work slot as cwd, environment),
 * exactly as the campaign's measured commands hash it. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_command_digest(BqRetirementRowCommand const* command,
    char digest[SHA256_HEX_CAPACITY])
{
    bool ok = tp_retirement_command_fields_hash(command->arguments, command->argument_count,
                                                BQ_RETIREMENT_ROW_WORK_PATH, command->environment,
                                                command->environment_count, digest) != 0;
    return ok;
}

/* Resolve and hash one step. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_step_digest(BqRetirementRowTemplate const* template,
    BqRetirementRowContext const* context, char* storage, char digest[SHA256_HEX_CAPACITY])
{
    BqRetirementRowCommand command;
    bool ok = bq_retirement_row_command(template, context, storage, &command) &&
              bq_retirement_row_command_digest(&command, digest);
    return ok;
}

/* The fixed output and per-row metrics leaves of row. */
BUSTER_GLOBAL_LOCAL void bq_retirement_row_leaves(u32 row, char output[32], char metrics[32])
{
    snprintf(output, 32, "row-%08u.out", row);
    snprintf(metrics, 32, "row-%08u.metrics", row);
}

/* Splits value in place into count fields at single spaces; the last field
 * is the rest of the line and may hold spaces. Every field is non-empty. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_fields(char* value, char** fields, u32 count)
{
    bool ok = value != NULL && count > 0;
    char* cursor = value;
    for (u32 index = 0; ok && index + 1u < count; index += 1)
    {
        fields[index] = cursor;
        char* space = strchr(cursor, ' ');
        ok = space != NULL && space != cursor;
        if (ok)
        {
            *space = 0;
            cursor = space + 1;
        }
    }
    if (ok) fields[count - 1u] = cursor;
    ok = ok && cursor[0] != 0;
    return ok;
}

/* One template block, in place: template=, argv= with its arg= lines and
 * environment= with its env= lines. */
BUSTER_GLOBAL_LOCAL void bq_retirement_row_template_decode(BqRetirementCheckCursor* cursor,
    BqRetirementRowTemplate* template, u32 index)
{
    char* fields[4] = {0};
    u32 number = 0;
    char* value = bq_retirement_check_line(cursor, "template=");
    cursor->ok = cursor->ok && bq_retirement_check_fields(value, fields, 4) &&
                 bq_retirement_check_decimal(fields[0], index, &number) && number == index;
    if (cursor->ok)
    {
        template->kind = bq_retirement_check_name(fields[1], bq_retirement_row_kind_names,
                                                  BQ_RETIREMENT_ROW_TEMPLATE_COUNT);
        cursor->ok = template->kind &&
                     bq_retirement_check_decimal(fields[2], BQ_RETIREMENT_CHECK_TIMEOUT_MAX_SECONDS,
                                                 &template->timeout_seconds) && template->timeout_seconds > 0 &&
                     bq_retirement_check_decimal(fields[3], BQ_RETIREMENT_CHECK_MEMORY_MAX_MIB, &template->memory_mib) &&
                     template->memory_mib >= BQ_RETIREMENT_CHECK_MEMORY_MIN_MIB;
    }
    u32 seen = 0;
    template->argument_count = bq_retirement_check_count(cursor, "argv=", 1, BQ_RETIREMENT_CHECK_ARGUMENTS_CAP);
    for (u32 argument = 0; cursor->ok && argument < template->argument_count; argument += 1)
    {
        template->arguments[argument] = bq_retirement_check_line(cursor, "arg=");
        cursor->ok = cursor->ok && bq_retirement_row_field(template->arguments[argument], template->kind, false, &seen);
    }
    /* A compiler step executes the side's held binary, a runtime step the
     * artifact its compile step wrote, never another path. */
    cursor->ok = cursor->ok && !strcmp(template->arguments[0], template->kind == BQ_RETIREMENT_ROW_TEMPLATE_RUNTIME ?
                                                                "./{{output}}" : "{{binary}}");
    template->environment_count = bq_retirement_check_count(cursor, "environment=", 0,
                                                            BQ_RETIREMENT_CHECK_ENVIRONMENT_CAP);
    for (u32 entry = 0; cursor->ok && entry < template->environment_count; entry += 1)
    {
        char const* text = bq_retirement_check_line(cursor, "env=");
        size_t name = bq_retirement_check_environment_name(text);
        cursor->ok = cursor->ok && name && bq_retirement_row_field(text + name + 1u, template->kind, true, &seen) &&
                     (!entry || strcmp(template->environment[entry - 1u], text) < 0);
        for (u32 previous = 0; cursor->ok && previous < entry; previous += 1)
            cursor->ok = !(bq_retirement_check_environment_name(template->environment[previous]) == name &&
                           !memcmp(template->environment[previous], text, name));
        template->environment[entry] = text;
    }
    cursor->ok = cursor->ok && (seen & bq_retirement_row_required_tokens[template->kind]) ==
                               bq_retirement_row_required_tokens[template->kind];
    if (cursor->ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-row-template-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_retirement_correctness_number(&hash, template->kind);
        bq_retirement_correctness_number(&hash, template->timeout_seconds);
        bq_retirement_correctness_number(&hash, template->memory_mib);
        bq_retirement_correctness_number(&hash, template->argument_count);
        for (u32 argument = 0; argument < template->argument_count; argument += 1)
            sha256_add(&hash, template->arguments[argument], strlen(template->arguments[argument]) + 1u);
        bq_retirement_correctness_number(&hash, template->environment_count);
        for (u32 entry = 0; entry < template->environment_count; entry += 1)
            sha256_add(&hash, template->environment[entry], strlen(template->environment[entry]) + 1u);
        sha256_finish_hex(&hash, template->template_sha256);
    }
}

/* "-" or a template index of kind. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_template_field(BqRetirementRowPlan const* plan, char const* text, u32 kind,
    bool batch, u32* value)
{
    bool ok = true;
    *value = BQ_RETIREMENT_ROW_PLAN_NONE;
    if (batch && !strcmp(text, "batch")) *value = BQ_RETIREMENT_ROW_PLAN_BATCH;
    else if (strcmp(text, "-"))
        ok = plan->template_count && bq_retirement_check_decimal(text, plan->template_count - 1u, value) &&
             plan->templates[*value].kind == kind;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_row_plan_decode_rows(BqRetirementCheckCursor* cursor, BqRetirementRowPlan* plan,
    u32 rows)
{
    u32 count = bq_retirement_check_count(cursor, "rows=", rows, rows);
    plan->rows = cursor->ok ? calloc(count, sizeof(*plan->rows)) : NULL;
    cursor->ok = cursor->ok && plan->rows;
    for (u32 index = 0; cursor->ok && index < count; index += 1)
    {
        BqRetirementRowPlanRow* row = plan->rows + index;
        char* fields[4] = {0};
        u32 number = 0;
        char* value = bq_retirement_check_line(cursor, "row=");
        cursor->ok = cursor->ok && bq_retirement_row_fields(value, fields, 4) &&
                     bq_retirement_check_decimal(fields[0], index, &number) && number == index &&
                     bq_retirement_row_template_field(plan, fields[1], BQ_RETIREMENT_ROW_TEMPLATE_COMPILE, true,
                                                      &row->compile) &&
                     bq_retirement_row_template_field(plan, fields[2], BQ_RETIREMENT_ROW_TEMPLATE_RUNTIME, false,
                                                      &row->runtime);
        row->fixture = cursor->ok && strcmp(fields[3], "-") ? fields[3] : NULL;
        row->group = BQ_RETIREMENT_ROW_PLAN_NONE;
        row->input = BQ_RETIREMENT_ROW_PLAN_NONE;
        cursor->ok = cursor->ok && (!row->fixture || tp_retirement_metrics_fixture(row->fixture));
    }
    plan->row_count = cursor->ok ? count : 0;
    return cursor->ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_row_plan_decode_groups(BqRetirementCheckCursor* cursor, BqRetirementRowPlan* plan)
{
    u32 count = bq_retirement_check_count(cursor, "groups=", 0, plan->row_count);
    plan->groups = cursor->ok && count ? calloc(count, sizeof(*plan->groups)) : NULL;
    cursor->ok = cursor->ok && (!count || plan->groups);
    u32 inputs = 0;
    for (u32 index = 0; cursor->ok && index < count; index += 1)
    {
        BqRetirementRowPlanGroup* group = plan->groups + index;
        char* fields[7] = {0};
        u32 number = 0;
        u64 bound = 0;
        char* value = bq_retirement_check_line(cursor, "group=");
        cursor->ok = cursor->ok && bq_retirement_check_fields(value, fields, 7) &&
                     bq_retirement_check_decimal(fields[0], index, &number) && number == index &&
                     plan->template_count && bq_retirement_check_decimal(fields[1], plan->template_count - 1u,
                                                                         &group->template_index) &&
                     plan->templates[group->template_index].kind == BQ_RETIREMENT_ROW_TEMPLATE_BATCH &&
                     tp_retirement_metrics_word(fields[2], strlen(fields[2])) && tp_retirement_metrics_leaf(fields[3]) &&
                     fields[4][0] != '0' && bq_retirement_number(string_from_pointer(fields[4]), &bound) && bound &&
                     bound <= TP_RETIREMENT_METRICS_ARTIFACT_BYTES &&
                     bq_retirement_check_decimal(fields[5], 255, &group->exit_status) &&
                     bq_retirement_check_decimal(fields[6], TP_RETIREMENT_BATCH_INPUTS, &group->input_count) &&
                     group->input_count > 0;
        BqRetirementRowPlanInput* grown = cursor->ok ?
            realloc(plan->inputs, ((size_t)inputs + group->input_count) * sizeof(*plan->inputs)) : NULL;
        cursor->ok = cursor->ok && grown;
        if (grown)
        {
            plan->inputs = grown;
            memset(plan->inputs + inputs, 0, (size_t)group->input_count * sizeof(*plan->inputs));
        }
        group->allocator = fields[2];
        group->metrics = fields[3];
        group->metrics_bytes_max = bound;
        group->first_input = inputs;
        for (u32 slot = 0; cursor->ok && slot < group->input_count; slot += 1)
        {
            BqRetirementRowPlanInput* input = plan->inputs + inputs + slot;
            char* input_fields[6] = {0};
            char* line = bq_retirement_check_line(cursor, "input=");
            cursor->ok = cursor->ok && bq_retirement_row_fields(line, input_fields, 6) &&
                         (!strcmp(input_fields[0], "-") ||
                          bq_retirement_check_decimal(input_fields[0], plan->row_count - 1u, &input->row)) &&
                         bq_retirement_check_decimal(input_fields[1], 1, &input->member) &&
                         tp_retirement_metrics_status(input_fields[2]) && tp_retirement_metrics_error(input_fields[3]) &&
                         (!strcmp(input_fields[4], "-") || tp_retirement_metrics_leaf(input_fields[4])) &&
                         tp_retirement_metrics_fixture(input_fields[5]);
            if (cursor->ok)
            {
                if (!strcmp(input_fields[0], "-")) input->row = TP_RETIREMENT_BATCH_NO_ROW;
                input->status = input_fields[2];
                input->error = input_fields[3];
                input->artifact = strcmp(input_fields[4], "-") ? input_fields[4] : NULL;
                input->fixture = input_fields[5];
            }
        }
        inputs += cursor->ok ? group->input_count : 0;
    }
    plan->group_count = cursor->ok ? count : 0;
    plan->input_count = cursor->ok ? inputs : 0;
    return cursor->ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_row_plan_decode(BqRetirementCheckCursor* cursor,
    BqRetirementProjection const* projection, BqRetirementRowPlan* plan)
{
    BqRetirementPrepared const* prepared = &projection->prepared;
    String8 line = {0};
    cursor->ok = cursor->ok && bq_next_line(cursor->bytes, &cursor->offset, &line) &&
                 string_equal(line, S8("BQ-RETIREMENT-ROW-PLAN-V1"));
    bq_retirement_check_digest_line(cursor, "support=", prepared->support_sha256);
    bq_retirement_check_digest_line(cursor, "census=", prepared->census_sha256);
    bq_retirement_check_digest_line(cursor, "population=", projection->population_sha256);
    u32 native = bq_retirement_check_count(cursor, "native-target=", 1, 12);
    cursor->ok = cursor->ok && native == prepared->native_target && native == BQ_RETIREMENT_UNIT_NATIVE_TARGET;
    plan->native_target = native;
    char* cpu = bq_retirement_check_line(cursor, "cpu=");
    char* cpu_fields[2] = {0};
    cursor->ok = cursor->ok && bq_retirement_check_fields(cpu, cpu_fields, 2) &&
                 bq_retirement_hex(string_from_pointer(cpu_fields[0]), 64) &&
                 bq_retirement_check_decimal(cpu_fields[1], CPU_SETSIZE - 1, &plan->cpu);
    if (cursor->ok) memcpy(plan->cpu_model_sha256, cpu_fields[0], SHA256_HEX_CAPACITY);
    u32 templates = bq_retirement_check_count(cursor, "templates=", 1, BQ_RETIREMENT_ROW_PLAN_TEMPLATES_CAP);
    plan->templates = cursor->ok ? calloc(templates, sizeof(*plan->templates)) : NULL;
    cursor->ok = cursor->ok && plan->templates;
    for (u32 index = 0; cursor->ok && index < templates; index += 1)
        bq_retirement_row_template_decode(cursor, plan->templates + index, index);
    plan->template_count = cursor->ok ? templates : 0;
    cursor->ok = cursor->ok && bq_retirement_row_plan_decode_rows(cursor, plan, prepared->rows) &&
                 bq_retirement_row_plan_decode_groups(cursor, plan) && cursor->offset == cursor->bytes.length;
    return cursor->ok;
}

/* A timed object row: compiler eligible, on the native target, object stage. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_timed_object(BqRetirementTrustedRow const* row, u32 native)
{
    bool timed = row->compiler_eligible && row->target == native && row->stage == BQ_RETIREMENT_STAGE_OBJECT;
    return timed;
}

/* A row with a native generated-runtime obligation, as the gate decides it. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_native_runtime(BqRetirementTrustedRow const* row, u32 native)
{
    bool runtime = row->compiler_eligible && row->execution_obligation && row->stage != BQ_RETIREMENT_STAGE_OBJECT &&
                   row->target == native;
    return runtime;
}

/* The batch contract skeleton of group for side, with placeholder
 * diagnostics and objects (placeholder, 64 zeros). */
BUSTER_GLOBAL_LOCAL void bq_retirement_row_skeleton(BqRetirementRowPlan const* plan, BqRetirementRowPlanGroup const* group,
    TpRetirementBatchInput* inputs, char const* placeholder, TpRetirementBatchContract* contract)
{
    for (u32 slot = 0; slot < group->input_count; slot += 1)
    {
        BqRetirementRowPlanInput const* input = plan->inputs + group->first_input + slot;
        inputs[slot] = (TpRetirementBatchInput){input->fixture, input->status, input->error, placeholder,
            input->artifact ? placeholder : NULL, input->artifact, input->member, input->row};
    }
    *contract = (TpRetirementBatchContract){BQ_RETIREMENT_ROW_BATCH_TARGET, group->allocator, group->metrics, inputs,
                                            group->input_count, group->exit_status, group->metrics_bytes_max};
}

/* The plan against the projection and the A1 contract: every row compiled
 * the way its eligibility, stage and target require, every batch row in
 * exactly one group input with its own fixture, valid group skeletons
 * ordered by their smallest member. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_plan_bind(BqRetirementRowPlan* plan, BqRetirementProjection const* projection)
{
    u32 native = projection->prepared.native_target;
    bool ok = plan->row_count == projection->prepared.rows;
    for (u32 index = 0; ok && index < plan->row_count; index += 1)
    {
        BqRetirementTrustedRow const* row = projection->rows + index;
        BqRetirementRowPlanRow const* planned = plan->rows + index;
        bool timed = bq_retirement_row_timed_object(row, native);
        bool control = !row->compiler_eligible && row->stage == BQ_RETIREMENT_STAGE_OBJECT && row->target == native;
        bool batch = planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH;
        bool template = planned->compile != BQ_RETIREMENT_ROW_PLAN_NONE && !batch;
        ok = (timed ? batch : batch ? control : row->compiler_eligible ? template : !template) &&
             (planned->runtime != BQ_RETIREMENT_ROW_PLAN_NONE) == bq_retirement_row_native_runtime(row, native) &&
             (planned->runtime == BQ_RETIREMENT_ROW_PLAN_NONE || template) &&
             (planned->fixture != NULL) == (planned->compile != BQ_RETIREMENT_ROW_PLAN_NONE);
    }
    TpRetirementBatchInput* inputs = ok ? calloc(TP_RETIREMENT_BATCH_INPUTS, sizeof(*inputs)) : NULL;
    ok = ok && (inputs || !plan->group_count);
    char placeholder[SHA256_HEX_CAPACITY];
    memset(placeholder, '0', 64);
    placeholder[64] = 0;
    u32 previous_first = 0;
    for (u32 index = 0; ok && index < plan->group_count; index += 1)
    {
        BqRetirementRowPlanGroup* group = plan->groups + index;
        for (u32 slot = 0; ok && slot < group->input_count; slot += 1)
        {
            BqRetirementRowPlanInput const* input = plan->inputs + group->first_input + slot;
            BqRetirementRowPlanRow* planned = input->row != TP_RETIREMENT_BATCH_NO_ROW ? plan->rows + input->row : NULL;
            BqRetirementTrustedRow const* row = planned ? projection->rows + input->row : NULL;
            ok = (!input->member || planned) &&
                 (!planned || (planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH &&
                               planned->group == BQ_RETIREMENT_ROW_PLAN_NONE && !strcmp(planned->fixture, input->fixture) &&
                               (input->member ? bq_retirement_row_timed_object(row, native) : !row->compiler_eligible)));
            if (ok && planned)
            {
                planned->group = index;
                planned->input = group->first_input + slot;
            }
        }
        TpRetirementBatchContract contract;
        if (ok) bq_retirement_row_skeleton(plan, group, inputs, placeholder, &contract);
        u32 first = ok ? plan->inputs[group->first_input].row : 0;
        ok = ok && tp_retirement_batch_contract_valid(&contract) && (!index || first > previous_first) &&
             tp_retirement_batch_input_list_leaf(&contract, group->list_leaf);
        previous_first = first;
    }
    free(inputs);
    for (u32 index = 0; ok && index < plan->row_count; index += 1)
        ok = plan->rows[index].compile != BQ_RETIREMENT_ROW_PLAN_BATCH || plan->rows[index].group != BQ_RETIREMENT_ROW_PLAN_NONE;
    return ok;
}

/* The derived identities: each group's batch key and command digests, every
 * row completed with its commands, key and control mark, and the second A/A
 * label aggregate in the campaign's order. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_plan_derive(BqRetirementRowPlan* plan, BqRetirementProjection const* projection)
{
    u32 native = projection->prepared.native_target;
    char* storage = malloc(BQ_RETIREMENT_ROW_COMMAND_STORAGE);
    plan->completed = calloc(plan->row_count, sizeof(*plan->completed));
    bool ok = storage && plan->completed;
    for (u32 index = 0; ok && index < plan->group_count; index += 1)
    {
        BqRetirementRowPlanGroup* group = plan->groups + index;
        BqRetirementRowTemplate const* template = plan->templates + group->template_index;
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-batch-key-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        sha256_add(&hash, template->template_sha256, 64);
        sha256_add(&hash, group->allocator, strlen(group->allocator) + 1u);
        sha256_finish_hex(&hash, group->key_sha256);
        for (u32 side = 0; ok && side < 2; side += 1)
        {
            BqRetirementRowContext context = {.metrics = group->metrics, .inputs = group->list_leaf, .side = side,
                                              .label = 1};
            ok = bq_retirement_row_step_digest(template, &context, storage, group->command_sha256[side]);
        }
        BqRetirementRowContext second = {.metrics = group->metrics, .inputs = group->list_leaf, .side = 0, .label = 2};
        ok = ok && bq_retirement_row_step_digest(template, &second, storage, group->second_sha256);
    }
    Sha256 aggregate;
    sha256_init(&aggregate);
    static char const aa_domain[] = BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN;
    sha256_add(&aggregate, aa_domain, sizeof(aa_domain) - 1);
    for (u32 index = 0; ok && index < plan->row_count; index += 1)
    {
        BqRetirementTrustedRow* row = plan->completed + index;
        BqRetirementRowPlanRow const* planned = plan->rows + index;
        *row = projection->rows[index];
        char output[32], metrics[32], second[2][SHA256_HEX_CAPACITY] = {{0}};
        bq_retirement_row_leaves(index, output, metrics);
        if (planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH)
        {
            BqRetirementRowPlanGroup const* group = plan->groups + planned->group;
            memcpy(row->batch_key_sha256, group->key_sha256, SHA256_HEX_CAPACITY);
            memcpy(row->compiler_command_sha256, group->command_sha256, sizeof(row->compiler_command_sha256));
            row->batch_control = !plan->inputs[planned->input].member;
        }
        else if (planned->compile != BQ_RETIREMENT_ROW_PLAN_NONE)
        {
            for (u32 label = 1; ok && label <= 2; label += 1)
                for (u32 side = 0; ok && side < (label == 1 ? 2u : 1u); side += 1)
                {
                    BqRetirementRowContext context = {.fixture = planned->fixture, .output = output, .metrics = metrics,
                                                      .side = side, .label = label};
                    ok = bq_retirement_row_step_digest(plan->templates + planned->compile, &context, storage,
                                                       label == 1 ? row->compiler_command_sha256[side] : second[0]);
                    if (ok && planned->runtime != BQ_RETIREMENT_ROW_PLAN_NONE)
                        ok = bq_retirement_row_step_digest(plan->templates + planned->runtime, &context, storage,
                                                           label == 1 ? row->runtime_command_sha256[side] : second[1]);
                }
        }
        /* The campaign's order: each timed object group at its first
         * member, each timed singleton at its row. */
        bool timed = row->compiler_eligible && row->target == native;
        bool object = row->stage == BQ_RETIREMENT_STAGE_OBJECT;
        bool first = object && planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH &&
                     plan->inputs[plan->groups[planned->group].first_input].row == index;
        if (ok && timed && (!object || first))
        {
            u8 ordinal[4] = {(u8)index, (u8)(index >> 8), (u8)(index >> 16), (u8)(index >> 24)};
            u8 runtime = !object && planned->runtime != BQ_RETIREMENT_ROW_PLAN_NONE;
            sha256_add(&aggregate, ordinal, sizeof(ordinal));
            sha256_add(&aggregate, object ? plan->groups[planned->group].second_sha256 : second[0], 64);
            sha256_add(&aggregate, &runtime, 1);
            if (runtime) sha256_add(&aggregate, second[1], 64);
        }
    }
    if (ok) sha256_finish_hex(&aggregate, plan->aa_second_commands_sha256);
    char population[SHA256_HEX_CAPACITY] = {0};
    ok = ok && bq_retirement_oracle_population_hash(plan->completed, plan->row_count, population) &&
         !memcmp(population, projection->population_sha256, SHA256_HEX_CAPACITY);
    free(storage);
    return ok;
}

bool bq_retirement_row_plan_release(BqRetirementRowPlan* plan)
{
    bool ok = plan != NULL;
    if (plan)
    {
        free(plan->text);
        free(plan->templates);
        free(plan->rows);
        free(plan->groups);
        free(plan->inputs);
        free(plan->completed);
        *plan = (BqRetirementRowPlan){0};
    }
    return ok;
}

/* Decodes, binds and derives the canonical bytes of a plan into imported
 * (which carries the job and attempt), exactly as the importer does after
 * its pin check; the offline generator (retirement_records.c) runs the same
 * steps over its own output before emitting it. imported->text receives a
 * NUL-split copy. BQ_RECIPE_MISMATCH for any other bytes. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_row_plan_parse(u8 const* bytes, u32 length,
    BqRetirementProjection const* projection, BqRetirementRowPlan* imported)
{
    imported->text = bytes && length ? malloc((size_t)length + 1u) : NULL;
    BqError result = !bytes || !length ? BQ_RECIPE_MISMATCH : imported->text ? BQ_OK : BQ_IO;
    if (result == BQ_OK)
    {
        memcpy(imported->text, bytes, length);
        imported->text[length] = 0;
        for (u32 index = 0; index < length; index += 1)
            if (imported->text[index] == '\n') imported->text[index] = 0;
        BqRetirementCheckCursor cursor = {{(char8*)bytes, length}, imported->text, 0, memchr(bytes, 0, length) == NULL};
        result = bq_retirement_row_plan_decode(&cursor, projection, imported) &&
                 bq_retirement_row_plan_bind(imported, projection) ? BQ_OK : BQ_RECIPE_MISMATCH;
    }
    if (result == BQ_OK) result = bq_retirement_row_plan_derive(imported, projection) ? BQ_OK : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK) memcpy(imported->population_sha256, projection->population_sha256, SHA256_HEX_CAPACITY);
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_row_plan_import_profile(int installed, String8 profile, BqJob const* job,
    BqRetirementProjection const* projection, BqRetirementRowPlan* plan)
{
    bool fresh = plan && !plan->owned;
    if (fresh) *plan = (BqRetirementRowPlan){0};
    char pin[SHA256_HEX_CAPACITY] = {0}, population[SHA256_HEX_CAPACITY] = {0};
    BqError result = fresh && installed >= 0 && job && projection && projection->owned && projection->rows &&
                     projection->job_id == job->id && projection->attempt_token == job->token ? BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK && !bq_retirement_profile_sha(profile, S8("row-plan-sha256="), pin)) result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK &&
        !(bq_retirement_oracle_population_hash(projection->rows, projection->prepared.rows, population) &&
          !memcmp(population, projection->population_sha256, SHA256_HEX_CAPACITY)))
        result = BQ_SOURCE_MISMATCH;
    BqRetirementRowPlan imported = {.job_id = job ? job->id : 0, .attempt_token = job ? job->token : 0};
    int recipes = result == BQ_OK ? openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    u8* bytes = NULL;
    u32 length = 0;
    if (result == BQ_OK)
        result = recipes >= 0 && bq_owned_directory(recipes, false, true) &&
                 bq_retirement_reference_read_installed(recipes, BQ_RETIREMENT_ROW_PLAN_NAME,
                     BQ_RETIREMENT_ROW_PLAN_BYTES_CAP, &bytes, &length, imported.authority_sha256, NULL) ?
                 BQ_OK : BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK && memcmp(imported.authority_sha256, pin, SHA256_HEX_CAPACITY)) result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_row_plan_parse(bytes, length, projection, &imported);
    free(bytes);
    if (recipes >= 0 && close(recipes) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK)
    {
        imported.owned = 1;
        *plan = imported;
    }
    else bq_retirement_row_plan_release(&imported);
    return result;
}

BqError bq_retirement_row_plan_import(int installed, BqJob const* job, BqRetirementProjection const* projection,
    BqRetirementRowPlan* plan)
{
    String8 profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BqError result = bq_retirement_row_plan_import_profile(installed, profile, job, projection, plan);
    return result;
}

bool bq_retirement_row_observed_init(BqRetirementRowPlan const* plan, BqRetirementRowObserved* observed)
{
    bool ok = plan && plan->owned && observed && !observed->owned;
    if (ok)
    {
        *observed = (BqRetirementRowObserved){.job_id = plan->job_id, .attempt_token = plan->attempt_token,
                                              .row_count = plan->row_count, .input_count = plan->input_count};
        observed->facts = calloc(plan->row_count, sizeof(*observed->facts));
        observed->diagnostic_sha256 = calloc(plan->input_count ? plan->input_count : 1u,
                                             sizeof(*observed->diagnostic_sha256));
        observed->object_sha256 = calloc(plan->input_count ? plan->input_count : 1u, sizeof(*observed->object_sha256));
        memcpy(observed->plan_sha256, plan->authority_sha256, SHA256_HEX_CAPACITY);
        memcpy(observed->population_sha256, plan->population_sha256, SHA256_HEX_CAPACITY);
        observed->owned = 1;
        ok = observed->facts && observed->diagnostic_sha256 && observed->object_sha256;
        if (!ok) bq_retirement_row_observed_release(observed);
    }
    return ok;
}

bool bq_retirement_row_observed_release(BqRetirementRowObserved* observed)
{
    bool ok = observed != NULL;
    if (observed)
    {
        if (observed->owned)
        {
            free(observed->facts);
            free(observed->diagnostic_sha256);
            free(observed->object_sha256);
        }
        *observed = (BqRetirementRowObserved){0};
    }
    return ok;
}

/* A growable text buffer; the first failure sticks. */
typedef struct BqRetirementRowText
{
    char* bytes;
    u64 length, capacity;
    bool ok;
} BqRetirementRowText;

BUSTER_GLOBAL_LOCAL void bq_retirement_row_text(BqRetirementRowText* text, char const* format, ...)
{
    char line[1024];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    text->ok = text->ok && length >= 0 && (size_t)length < sizeof(line) &&
               text->length + (u64)length <= BQ_RETIREMENT_ROW_EVIDENCE_BYTES_CAP;
    if (text->ok && text->length + (u64)length + 1u > text->capacity)
    {
        u64 capacity = text->capacity ? text->capacity * 2u : 65536u;
        while (capacity < text->length + (u64)length + 1u) capacity *= 2u;
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
        memcpy(text->bytes + text->length, line, (size_t)length + 1u);
        text->length += (u64)length;
    }
}

/* A digest field: "-" when empty. */
BUSTER_GLOBAL_LOCAL char const* bq_retirement_row_digest_text(char const value[SHA256_HEX_CAPACITY])
{
    char const* text = value[0] ? value : "-";
    return text;
}

bool bq_retirement_row_observed_format(BqRetirementRowObserved const* observed, char** text, u32* length)
{
    BqRetirementRowText out = {.ok = observed && observed->owned && text && length};
    if (out.ok)
    {
        bq_retirement_row_text(&out, "BQ-RETIREMENT-ROW-EVIDENCE-V1\njob=%" PRIu64 "\nattempt=%" PRIu64
                               "\nrow-plan=%s\npopulation=%s\ncpu=%s %u %s\nsandbox-abi=%u\nrows=%u\n",
                               (uint64_t)observed->job_id, (uint64_t)observed->attempt_token,
                               bq_retirement_row_digest_text(observed->plan_sha256),
                               bq_retirement_row_digest_text(observed->population_sha256),
                               bq_retirement_row_digest_text(observed->cpu_model_sha256), observed->cpus,
                               observed->cpu_mask[0] ? observed->cpu_mask : "-", observed->sandbox_abi,
                               observed->row_count);
    }
    for (u32 index = 0; out.ok && index < observed->row_count; index += 1)
    {
        BqRetirementRowFact const* fact = observed->facts + index;
        bq_retirement_row_text(&out, "fact=%u %u %u %u %u\n", fact->row, fact->census_row, fact->compiler_eligible,
                               fact->runtime_eligible, fact->code_eligible);
        for (u32 side = 0; side < 2; side += 1)
        {
            BqRetirementObservedSide const* observed_side = fact->side + side;
            bq_retirement_row_text(&out, "side=%s %s %s %s %s %s %" PRIu64 " %u %u %u %u %d %d\n",
                                   bq_retirement_row_digest_text(observed_side->compiler_command_sha256),
                                   bq_retirement_row_digest_text(observed_side->artifact_sha256),
                                   bq_retirement_row_digest_text(observed_side->code_sha256),
                                   bq_retirement_row_digest_text(observed_side->diagnostic_sha256),
                                   bq_retirement_row_digest_text(observed_side->runtime_command_sha256),
                                   bq_retirement_row_digest_text(observed_side->runtime_output_sha256),
                                   (uint64_t)observed_side->code_bytes, observed_side->semantic_pass,
                                   observed_side->fallback_count, observed_side->timed_out,
                                   observed_side->out_of_memory, observed_side->compiler_exit,
                                   observed_side->runtime_exit);
        }
    }
    if (out.ok) bq_retirement_row_text(&out, "inputs=%u\n", observed->input_count);
    for (u32 index = 0; out.ok && index < observed->input_count; index += 1)
        bq_retirement_row_text(&out, "input=%u %s %s %s %s\n", index,
                               bq_retirement_row_digest_text(observed->diagnostic_sha256[index][0]),
                               bq_retirement_row_digest_text(observed->object_sha256[index][0]),
                               bq_retirement_row_digest_text(observed->diagnostic_sha256[index][1]),
                               bq_retirement_row_digest_text(observed->object_sha256[index][1]));
    if (out.ok && out.length <= UINT32_MAX)
    {
        *text = out.bytes;
        *length = (u32)out.length;
    }
    else free(out.bytes);
    return out.ok && out.length <= UINT32_MAX;
}

/* A digest field into value: "-" is empty, else 64 lowercase hex. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_digest_field(char const* text, char value[SHA256_HEX_CAPACITY])
{
    bool empty = !strcmp(text, "-");
    bool ok = empty || bq_retirement_hex(string_from_pointer(text), 64);
    if (ok && empty) memset(value, 0, SHA256_HEX_CAPACITY);
    else if (ok) memcpy(value, text, SHA256_HEX_CAPACITY);
    return ok;
}

/* A decimal that may be negative; canonical form is enforced by the
 * formatter's round trip. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_signed(char const* text, int* value)
{
    char* end = NULL;
    errno = 0;
    long number = strtol(text, &end, 10);
    bool ok = text[0] && end && !*end && errno == 0 && number >= INT32_MIN && number <= INT32_MAX;
    *value = ok ? (int)number : 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_row_unsigned(char const* text, u64 maximum, u64* value)
{
    bool ok = text[0] && (text[0] != '0' || !text[1]) && bq_retirement_number(string_from_pointer(text), value) &&
              *value <= maximum;
    if (!ok) *value = 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_row_side_decode(BqRetirementCheckCursor* cursor, BqRetirementObservedSide* side)
{
    char* fields[13] = {0};
    char* value = bq_retirement_check_line(cursor, "side=");
    u64 numbers[5] = {0};
    cursor->ok = cursor->ok && bq_retirement_check_fields(value, fields, 13) &&
                 bq_retirement_row_digest_field(fields[0], side->compiler_command_sha256) &&
                 bq_retirement_row_digest_field(fields[1], side->artifact_sha256) &&
                 bq_retirement_row_digest_field(fields[2], side->code_sha256) &&
                 bq_retirement_row_digest_field(fields[3], side->diagnostic_sha256) &&
                 bq_retirement_row_digest_field(fields[4], side->runtime_command_sha256) &&
                 bq_retirement_row_digest_field(fields[5], side->runtime_output_sha256) &&
                 bq_retirement_row_unsigned(fields[6], UINT64_MAX, &numbers[0]) &&
                 bq_retirement_row_unsigned(fields[7], UINT32_MAX, &numbers[1]) &&
                 bq_retirement_row_unsigned(fields[8], UINT32_MAX, &numbers[2]) &&
                 bq_retirement_row_unsigned(fields[9], UINT32_MAX, &numbers[3]) &&
                 bq_retirement_row_unsigned(fields[10], UINT32_MAX, &numbers[4]) &&
                 bq_retirement_row_signed(fields[11], &side->compiler_exit) &&
                 bq_retirement_row_signed(fields[12], &side->runtime_exit);
    side->code_bytes = numbers[0];
    side->semantic_pass = (u32)numbers[1];
    side->fallback_count = (u32)numbers[2];
    side->timed_out = (u32)numbers[3];
    side->out_of_memory = (u32)numbers[4];
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_row_observed_decode(BqRetirementCheckCursor* cursor,
    BqRetirementRowObserved* observed)
{
    String8 line = {0};
    char* fields[5] = {0};
    u64 numbers[2] = {0};
    cursor->ok = cursor->ok && bq_next_line(cursor->bytes, &cursor->offset, &line) &&
                 string_equal(line, S8("BQ-RETIREMENT-ROW-EVIDENCE-V1"));
    char* job = bq_retirement_check_line(cursor, "job=");
    char* attempt = bq_retirement_check_line(cursor, "attempt=");
    cursor->ok = cursor->ok && bq_retirement_row_unsigned(job, UINT64_MAX, &numbers[0]) &&
                 bq_retirement_row_unsigned(attempt, UINT64_MAX, &numbers[1]) &&
                 numbers[0] == observed->job_id && numbers[1] == observed->attempt_token;
    bq_retirement_check_digest_line(cursor, "row-plan=", observed->plan_sha256);
    bq_retirement_check_digest_line(cursor, "population=", observed->population_sha256);
    char* cpu = bq_retirement_check_line(cursor, "cpu=");
    u64 cpus = 0;
    cursor->ok = cursor->ok && bq_retirement_check_fields(cpu, fields, 3) &&
                 bq_retirement_row_digest_field(fields[0], observed->cpu_model_sha256) &&
                 bq_retirement_row_unsigned(fields[1], CPU_SETSIZE, &cpus) &&
                 strlen(fields[2]) < sizeof(observed->cpu_mask);
    if (cursor->ok)
    {
        observed->cpus = (u32)cpus;
        snprintf(observed->cpu_mask, sizeof(observed->cpu_mask), "%s", strcmp(fields[2], "-") ? fields[2] : "");
    }
    observed->sandbox_abi = bq_retirement_check_count(cursor, "sandbox-abi=", 0, UINT32_MAX);
    u32 rows = bq_retirement_check_count(cursor, "rows=", observed->row_count, observed->row_count);
    for (u32 index = 0; cursor->ok && index < rows; index += 1)
    {
        BqRetirementRowFact* fact = observed->facts + index;
        char* value = bq_retirement_check_line(cursor, "fact=");
        cursor->ok = cursor->ok && bq_retirement_check_fields(value, fields, 5) &&
                     bq_retirement_check_decimal(fields[0], UINT32_MAX, &fact->row) &&
                     bq_retirement_check_decimal(fields[1], UINT32_MAX, &fact->census_row) &&
                     bq_retirement_check_decimal(fields[2], 1, &fact->compiler_eligible) &&
                     bq_retirement_check_decimal(fields[3], 1, &fact->runtime_eligible) &&
                     bq_retirement_check_decimal(fields[4], 1, &fact->code_eligible);
        for (u32 side = 0; side < 2; side += 1) bq_retirement_row_side_decode(cursor, fact->side + side);
    }
    u32 inputs = bq_retirement_check_count(cursor, "inputs=", observed->input_count, observed->input_count);
    for (u32 index = 0; cursor->ok && index < inputs; index += 1)
    {
        u32 number = 0;
        char* value = bq_retirement_check_line(cursor, "input=");
        cursor->ok = cursor->ok && bq_retirement_check_fields(value, fields, 5) &&
                     bq_retirement_check_decimal(fields[0], index, &number) && number == index &&
                     bq_retirement_row_digest_field(fields[1], observed->diagnostic_sha256[index][0]) &&
                     bq_retirement_row_digest_field(fields[2], observed->object_sha256[index][0]) &&
                     bq_retirement_row_digest_field(fields[3], observed->diagnostic_sha256[index][1]) &&
                     bq_retirement_row_digest_field(fields[4], observed->object_sha256[index][1]);
    }
    cursor->ok = cursor->ok && cursor->offset == cursor->bytes.length;
    return cursor->ok;
}

BqError bq_retirement_row_observed_parse(u8 const* bytes, u32 length, BqRetirementRowPlan const* plan,
    BqRetirementRowObserved* observed)
{
    BqRetirementRowObserved parsed = {0};
    char* text = bytes && length ? malloc((size_t)length + 1u) : NULL;
    BqError result = observed && !observed->owned && text && bq_retirement_row_observed_init(plan, &parsed) ?
                     BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK)
    {
        memcpy(text, bytes, length);
        text[length] = 0;
        for (u32 index = 0; index < length; index += 1)
            if (text[index] == '\n') text[index] = 0;
        BqRetirementCheckCursor cursor = {{(char8*)bytes, length}, text, 0, memchr(bytes, 0, length) == NULL};
        result = bq_retirement_row_observed_decode(&cursor, &parsed) ? BQ_OK : BQ_CORRUPT;
    }
    /* Only the canonical bytes: the parse must format back to exactly them. */
    char* again = NULL;
    u32 again_length = 0;
    if (result == BQ_OK)
        result = bq_retirement_row_observed_format(&parsed, &again, &again_length) && again_length == length &&
                 !memcmp(again, bytes, length) ? BQ_OK : BQ_CORRUPT;
    free(again);
    free(text);
    if (result == BQ_OK) *observed = parsed;
    else bq_retirement_row_observed_release(&parsed);
    return result;
}

bool bq_retirement_row_joined_release(BqRetirementRowJoined* joined)
{
    bool ok = joined != NULL;
    if (joined)
    {
        if (joined->owned)
        {
            free(joined->groups);
            free(joined->inputs);
            free(joined->strings);
        }
        *joined = (BqRetirementRowJoined){0};
    }
    return ok;
}

/* Copies text into the joined string arena. */
BUSTER_GLOBAL_LOCAL char const* bq_retirement_row_keep(char* strings, u64 capacity, u64* used, char const* text)
{
    size_t length = text ? strlen(text) + 1u : 0;
    char* kept = text && *used + length <= capacity ? strings + *used : NULL;
    if (kept)
    {
        memcpy(kept, text, length);
        *used += length;
    }
    return kept;
}

/* The mask of exactly one CPU, as bq_retirement_check_provenance prints it. */
BUSTER_GLOBAL_LOCAL void bq_retirement_row_cpu_mask(u32 cpu, char mask[BQ_RETIREMENT_ROW_CPU_MASK_CAPACITY])
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    u8 bytes[sizeof(cpu_set_t)];
    memcpy(bytes, &set, sizeof(bytes));
    static char const hex[] = "0123456789abcdef";
    for (size_t index = 0; index < sizeof(bytes); index += 1)
    {
        mask[2u * index] = hex[bytes[index] >> 4];
        mask[2u * index + 1u] = hex[bytes[index] & 15u];
    }
    mask[2u * sizeof(bytes)] = 0;
}

BqError bq_retirement_row_evidence_join(BqRetirementRowPlan const* plan, BqRetirementProjection const* projection,
    BqRetirementRowObserved const* observed, BqRetirementRowJoined* joined)
{
    bool fresh = joined && !joined->owned;
    if (fresh) *joined = (BqRetirementRowJoined){0};
    BqError result = fresh && plan && plan->owned && projection && projection->owned && observed && observed->owned &&
                     plan->job_id == projection->job_id && plan->attempt_token == projection->attempt_token &&
                     !memcmp(plan->population_sha256, projection->population_sha256, SHA256_HEX_CAPACITY) ?
                     BQ_OK : BQ_BAD_REQUEST;
    /* The observation of exactly this plan and attempt. */
    if (result == BQ_OK)
        result = observed->job_id == plan->job_id && observed->attempt_token == plan->attempt_token &&
                 !memcmp(observed->plan_sha256, plan->authority_sha256, SHA256_HEX_CAPACITY) &&
                 !memcmp(observed->population_sha256, plan->population_sha256, SHA256_HEX_CAPACITY) &&
                 observed->row_count == plan->row_count && observed->input_count == plan->input_count ?
                 BQ_OK : BQ_RECIPE_MISMATCH;
    /* CPU provenance: the rows ran on the plan's CPU alone, of its model. */
    char mask[BQ_RETIREMENT_ROW_CPU_MASK_CAPACITY];
    if (result == BQ_OK) bq_retirement_row_cpu_mask(plan->cpu, mask);
    if (result == BQ_OK && !(observed->cpus == 1 && !strcmp(observed->cpu_mask, mask) &&
                             !memcmp(observed->cpu_model_sha256, plan->cpu_model_sha256, SHA256_HEX_CAPACITY)))
        result = BQ_CONFIGURATION_MISMATCH;
    /* The steps ran in the whole sandbox. */
    if (result == BQ_OK && observed->sandbox_abi < BQ_RETIREMENT_SANDBOX_MIN_ABI) result = BQ_CONFIGURATION_MISMATCH;
    u64 capacity = 0, used = 0;
    for (u32 index = 0; result == BQ_OK && index < plan->group_count; index += 1)
        capacity += strlen(plan->groups[index].allocator) + strlen(plan->groups[index].metrics) + 2u;
    for (u32 index = 0; result == BQ_OK && index < plan->input_count; index += 1)
    {
        BqRetirementRowPlanInput const* input = plan->inputs + index;
        capacity += 2u * (strlen(input->fixture) + strlen(input->status) + strlen(input->error) + 4u +
                          (input->artifact ? strlen(input->artifact) : 0) + 2u * SHA256_HEX_CAPACITY);
    }
    BqRetirementRowJoined built = {.owned = 1};
    if (result == BQ_OK)
    {
        built.groups = plan->group_count ? calloc(plan->group_count, sizeof(*built.groups)) : NULL;
        built.inputs = plan->input_count ? calloc(2u * (size_t)plan->input_count, sizeof(*built.inputs)) : NULL;
        built.strings = capacity ? malloc((size_t)capacity) : NULL;
        if ((plan->group_count && !built.groups) || (plan->input_count && !built.inputs) || (capacity && !built.strings))
            result = BQ_IO;
    }
    for (u32 index = 0; result == BQ_OK && index < plan->group_count; index += 1)
    {
        BqRetirementRowPlanGroup const* group = plan->groups + index;
        BqRetirementBatchGroup* frozen = built.groups + index;
        char const* allocator = bq_retirement_row_keep(built.strings, capacity, &used, group->allocator);
        char const* metrics = bq_retirement_row_keep(built.strings, capacity, &used, group->metrics);
        for (u32 side = 0; side < 2; side += 1)
        {
            TpRetirementBatchInput* inputs = built.inputs + (size_t)side * plan->input_count + group->first_input;
            for (u32 slot = 0; slot < group->input_count; slot += 1)
            {
                u32 at = group->first_input + slot;
                BqRetirementRowPlanInput const* input = plan->inputs + at;
                char const* object = observed->object_sha256[at][side];
                inputs[slot] = (TpRetirementBatchInput){
                    bq_retirement_row_keep(built.strings, capacity, &used, input->fixture),
                    bq_retirement_row_keep(built.strings, capacity, &used, input->status),
                    bq_retirement_row_keep(built.strings, capacity, &used, input->error),
                    bq_retirement_row_keep(built.strings, capacity, &used, observed->diagnostic_sha256[at][side]),
                    object[0] ? bq_retirement_row_keep(built.strings, capacity, &used, object) : NULL,
                    input->artifact ? bq_retirement_row_keep(built.strings, capacity, &used, input->artifact) : NULL,
                    input->member, input->row};
            }
            frozen->contract[side] = (TpRetirementBatchContract){BQ_RETIREMENT_ROW_BATCH_TARGET, allocator, metrics,
                inputs, group->input_count, group->exit_status, group->metrics_bytes_max};
            memcpy(frozen->command_sha256[side], group->command_sha256[side], SHA256_HEX_CAPACITY);
        }
    }
    if (result == BQ_OK)
    {
        built.evidence = (BqRetirementRowEvidence){plan->completed, observed->facts, built.groups, plan->row_count,
                                                   plan->group_count, {0}, {0}};
        memcpy(built.evidence.aa_second_commands_sha256, plan->aa_second_commands_sha256, SHA256_HEX_CAPACITY);
        memcpy(built.evidence.plan_sha256, plan->authority_sha256, SHA256_HEX_CAPACITY);
        *joined = built;
    }
    else bq_retirement_row_joined_release(&built);
    return result;
}
