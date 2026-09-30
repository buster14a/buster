/* In-unit #881-D campaign of the retirement producer (#881, PRs 2 and 3).
 *
 * Ownership: the worker-unit producer's SETTLING and MEASURING stages, from
 * lane B's ready record to lane D's READY, then composition to MEASURED
 * (retirement_worker_compose.c). retirement_worker_unit.c calls
 * bq_retirement_worker_campaign_run after the ready record; everything here
 * is service code compiled only into the service translation unit (main.c
 * includes this file after retirement_campaign_service.h and before
 * retirement_worker_unit.c). It builds, from pinned and sealed inputs only,
 * what lane D's driver (retirement_unit_campaign.h) takes as caller data, and
 * sequences the driver:
 *   SETTLING   bq_retirement_unit_campaign_begin, the store-based import
 *              (second holds on both binaries, the imported ready record),
 *              the row plan re-imported (the gate releases its copy), the
 *              reviewed budget (bq_retirement_worker_budget_load), the
 *              pinned rows, the untimed batches
 *              (bq_retirement_worker_untimed_build), tp_retirement_untimed_init
 *              and bq_retirement_unit_campaign_untimed
 *   MEASURING  bq_retirement_unit_campaign_measuring,
 *              bq_retirement_campaign_plan_commands, both stages allocated
 *              and bound after the acknowledgement
 *              (bq_retirement_worker_stages_open), the store-based bind, the
 *              documents sized (bq_retirement_unit_campaign_documents_measure,
 *              retained as documents-sized.txt, bq_retirement_worker_sized_write),
 *              lane E's store plan (bq_retirement_worker_store_plan), attach,
 *              documents, A/A, admission, post-A/A document, freeze, A/B and
 *              READY on the post-sample record stream
 *   MEASURED   (#881 PR 3) bq_retirement_worker_compose
 *              (retirement_worker_compose.c): the binding, composition, the
 *              receipt authority and context chain, MEASURED carrying the
 *              authority digest, then the result manifest and bundle
 * The retirement-worker entries of the result root are reserved before
 * timing (BQ_RETIREMENT_WORKER_RESULT_ENTRIES: the control files, the phase
 * receipts, the binding and its admission receipt; and the evidence files
 * the pinned binding context names, bq_retirement_worker_evidence_measure). Production A/A
 * admission has no authority (#426, #1021) and stays compiled out; the
 * preparation fixture compiles the driver's fixture admission and supplies
 * the receipt stand-in (bq_retirement_worker_campaign_fixture_receipt).
 *
 * Entry point: bq_retirement_worker_campaign_run. Release in reverse on
 * every path: bq_retirement_worker_campaign_release (store abort unless
 * composed, campaign poison, ready record, held binaries, plans, streams,
 * arena); orphans a
 * killed launch left reparented to the producer's subreaper are killed and
 * reaped first (bq_retirement_check_sweep), and a failure is retained as
 * retirement-campaign/campaign-failure.txt in the attempt
 * (bq_retirement_worker_failure_write). Launches take the producer's
 * self-pipe as their cancellation descriptor, so tp_process leaves SIGTERM
 * to the producer (tools/throughput/platform.h).
 *
 * Map: bq_retirement_worker_budget_load (the reviewed budget record, pinned
 * by campaign-budget-sha256=); BqRetirementWorkerUntimedContract and
 * bq_retirement_worker_untimed_import (the untimed-command contract, pinned
 * by untimed-commands-sha256=: the untimed object groups' batch templates,
 * metrics words and artifact leaves, which lane B's row plan does not
 * carry); BqRetirementWorkerUntimed and bq_retirement_worker_untimed_build;
 * BqRetirementWorkerLayout and bq_retirement_worker_layout_build (the timed
 * sample layout and runtime rows from the sealed gate);
 * BqRetirementWorkerStage and bq_retirement_worker_stages_open (arena
 * storage sized from the layout, capped by lane D's own limits);
 * BqRetirementWorkerStreamGroup, bq_retirement_worker_streams_open and
 * bq_retirement_worker_streams_publish (every stream a private staged file
 * named by its store path, published into the result store in index order
 * once complete: the store admits one pending file at a time, so streams
 * written concurrently cannot all be pending store files);
 * bq_retirement_worker_store_plan (lane E's plan with the retained
 * declaration, D's documents as prior entries and the result root's control
 * files, phase receipts and evidence files reserved); BqRetirementWorkerCampaign.
 */
#include "worker_linux.h"

/* The attempt's campaign directory and its private sub-directories: the
 * launches' work directory (their cwd), the launch logs, the untimed code
 * objects, the staged streams and the sample spools. */
#define BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY "retirement-campaign"
enum
{
    BQ_RETIREMENT_WORKER_WORK,
    BQ_RETIREMENT_WORKER_LOGS,
    BQ_RETIREMENT_WORKER_CODE,
    BQ_RETIREMENT_WORKER_STREAMS,
    BQ_RETIREMENT_WORKER_SCRATCH,
    BQ_RETIREMENT_WORKER_DIRECTORIES
};
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_worker_directories[BQ_RETIREMENT_WORKER_DIRECTORIES] = {
    "work", "logs", "code", "streams", "scratch"};
/* The retained failure (lane D's BqRetirementUnitCampaignFailure and the
 * producer's result), one canonical key=value file. */
#define BQ_RETIREMENT_WORKER_FAILURE_NAME "campaign-failure.txt"
#define BQ_RETIREMENT_WORKER_FAILURE_HEADER "BQ-RETIREMENT-UNIT-CAMPAIGN-FAILURE-V1\n"
#define BQ_RETIREMENT_WORKER_FAILURE_BYTES 2048u
/* The reviewed campaign budget record, installed under recipes/ and pinned
 * by the profile (retirement_budget.h's canonical encoding). */
#define BQ_RETIREMENT_WORKER_BUDGET_NAME "native-retirement-performance-v1.campaign-budget"
#define BQ_RETIREMENT_WORKER_BUDGET_PIN "campaign-budget-sha256="
/* The untimed-command contract, installed under recipes/ and pinned by the
 * profile: for every untimed object group of the validator's untimed
 * partition (bq_retirement_documents_partition), in partition order,
 *   group=<partition-group> <row-plan batch template> <target> <allocator>
 *         <metrics-leaf> <members>
 *   input=<row> <artifact-leaf>        (once per member, in row order)
 * after the header and `row-plan=<row-plan authority digest>` and
 * `groups=<count>`. Untimed singletons use their row's compile template from
 * the row plan, as timed singletons do. */
#define BQ_RETIREMENT_WORKER_UNTIMED_NAME "native-retirement-performance-v1.untimed-commands"
#define BQ_RETIREMENT_WORKER_UNTIMED_PIN "untimed-commands-sha256="
#define BQ_RETIREMENT_WORKER_UNTIMED_HEADER "BQ-RETIREMENT-UNTIMED-COMMANDS-V1"
#define BQ_RETIREMENT_WORKER_UNTIMED_BYTES_CAP (16u * 1024u * 1024u)
/* One reservation for every campaign allocation (the arena commits only what
 * is used; tp_retirement_compose_allocate refuses beyond it). */
#define BQ_RETIREMENT_WORKER_ARENA_BYTES (UINT64_C(1) << 32)
/* Staged streams of one attempt: never more than the store holds. */
#define BQ_RETIREMENT_WORKER_STREAMS_MAX TP_RETIREMENT_STORE_FILES
/* The result root's worker-written entries (the control files: manifest,
 * bundle and outcome; the coordinator's five BQPHASE2 worker-phase-N
 * receipts; and the producer's #511 binding document and A/A admission
 * receipt, retirement_worker_compose.c) are inventoried beside the store's
 * files, so the store plan reserves them as external entries, each at the
 * bundle's per-file cap: the store then keeps its files plus those entries
 * within the bundle's entry cap. The evidence files the pinned binding
 * context names (bq_retirement_worker_evidence_measure) are external entries
 * too, at their exact sizes. D's five documents are the plan's prior entries
 * and the authority and its context chain live in the attempt workspace,
 * outside the result root. */
#define BQ_RETIREMENT_WORKER_BINDING_ENTRIES 2u
#define BQ_RETIREMENT_WORKER_RESULT_ENTRIES \
    (BQ_WORKER_BUNDLE_CONTROL_ENTRIES + BQ_WORKER_RETIREMENT_PHASE_RECEIPTS + BQ_RETIREMENT_WORKER_BINDING_ENTRIES)
#define BQ_RETIREMENT_WORKER_CONTROL_BYTES ((u64)BQ_RETIREMENT_WORKER_RESULT_ENTRIES * BQ_WORKER_BUNDLE_FILE_CAP)
/* The most evidence files a binding context may name (the #511 record names
 * 39: nine support files, the validator source, six closures, both subjects'
 * snapshot, binary and build receipt, four producer, two measurement, four
 * execution and five provenance artifacts, the contract source and the
 * admission record). */
#define BQ_RETIREMENT_WORKER_EVIDENCE_CAP 48u
BUSTER_CT_CHECK(TP_RETIREMENT_STORE_FILES <= BQ_WORKER_BUNDLE_ENTRY_CAP);
BUSTER_CT_CHECK(BQ_RETIREMENT_WORKER_RESULT_ENTRIES >= TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES);
/* One receipt per BQPHASE2 phase, RETIREMENT_READY the last-numbered. */
BUSTER_CT_CHECK(BQ_WORKER_RETIREMENT_PHASE_RECEIPTS == BQ_PHASE_RETIREMENT_READY);
/* The untimed records stream's store path. */
#define BQ_RETIREMENT_WORKER_UNTIMED_PATH "retirement-untimed-batches.jsonl"
/* A/A and A/B writer tags and sample-shard prefixes: A/A's are retained
 * (declared) store files, A/B's are lane E's sealed inputs. */
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_worker_tags[TP_RETIREMENT_CAMPAIGN_STAGES] = {"aa", "ab"};
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_worker_sample_tags[TP_RETIREMENT_CAMPAIGN_STAGES] = {"aa-", ""};

#if defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA)
/* Fixture only (the installed service cannot define the flag): the #437
 * receipt stand-in over the driver's family and the campaign's CPU, defined
 * by the preparation fixture (retirement_worker_unit_tests.h). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_campaign_fixture_receipt(BqRetirementUnitCampaign const* driver,
    TpRetirementCampaign const* campaign, char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX],
    u32* length, char digest[SHA256_HEX_CAPACITY]);
#endif

BUSTER_GLOBAL_LOCAL void* bq_retirement_worker_allocate(Arena* arena, u64 count, u64 size)
{
    void* result = arena && count && size && count <= UINT64_MAX / size ?
                   tp_retirement_compose_allocate(arena, count * size) : NULL;
    return result;
}

/* ----------------------------------------------------------------- budget */

/* The reviewed budget: the installed record must hash to the profile's pin
 * and decode strictly (its re-encoding is the file). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_budget_load(int installed, String8 profile,
    TpRetirementCampaignBudget* budget)
{
    char pin[SHA256_HEX_CAPACITY] = {0}, digest[SHA256_HEX_CAPACITY] = {0};
    BqError result = budget && installed >= 0 &&
                     bq_retirement_profile_sha(profile, S8(BQ_RETIREMENT_WORKER_BUDGET_PIN), pin) ?
                     BQ_OK : BQ_RECIPE_MISMATCH;
    int recipes = result == BQ_OK ? openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    u8* bytes = NULL;
    u32 length = 0;
    if (result == BQ_OK)
        result = recipes >= 0 && bq_owned_directory(recipes, false, true) &&
                 bq_retirement_reference_read_installed(recipes, BQ_RETIREMENT_WORKER_BUDGET_NAME,
                     TP_RETIREMENT_BUDGET_BYTES - 1u, &bytes, &length, digest, NULL) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK && memcmp(digest, pin, SHA256_HEX_CAPACITY)) result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !tp_retirement_budget_decode((char const*)bytes, length, budget)) result = BQ_RECIPE_MISMATCH;
    free(bytes);
    if (recipes >= 0 && close(recipes) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    return result;
}

/* -------------------------------------------------------- untimed contract */

/* One untimed object group of the contract: its partition group, row-plan
 * batch template, the metrics header's target and allocator words, its
 * metrics leaf and its members (contract inputs first .. first + count). */
typedef struct BqRetirementWorkerUntimedGroup
{
    char const* target;
    char const* allocator;
    char const* metrics;
    u32 partition, template_index, first, count;
} BqRetirementWorkerUntimedGroup;

/* The imported contract; every string points into the arena copy of the
 * pinned file. */
typedef struct BqRetirementWorkerUntimedContract
{
    BqRetirementWorkerUntimedGroup* groups;
    char const** artifacts;
    u32* rows;
    u32 group_count, input_count;
    char authority_sha256[SHA256_HEX_CAPACITY];
} BqRetirementWorkerUntimedContract;

/* One group line and its member lines, checked against untimed partition
 * group `group` and the row plan's batch templates. */
BUSTER_GLOBAL_LOCAL void bq_retirement_worker_untimed_group(BqRetirementCheckCursor* cursor,
    BqRetirementRowPlan const* plan, BqRetirementDocumentPartition const* untimed, u32 group,
    BqRetirementWorkerUntimedContract* contract, BqRetirementWorkerUntimedGroup* entry)
{
    char* fields[6] = {0};
    u32 number = UINT32_MAX, members = 0;
    char* value = bq_retirement_check_line(cursor, "group=");
    u32 first = untimed->first[group], count = untimed->first[group + 1] - first;
    cursor->ok = cursor->ok && bq_retirement_check_fields(value, fields, 6) &&
                 bq_retirement_check_decimal(fields[0], untimed->count - 1u, &number) && number == group &&
                 plan->template_count &&
                 bq_retirement_check_decimal(fields[1], plan->template_count - 1u, &entry->template_index) &&
                 plan->templates[entry->template_index].kind == BQ_RETIREMENT_ROW_TEMPLATE_BATCH &&
                 tp_retirement_metrics_word(fields[2], strlen(fields[2])) &&
                 tp_retirement_metrics_word(fields[3], strlen(fields[3])) && tp_retirement_metrics_leaf(fields[4]) &&
                 bq_retirement_check_decimal(fields[5], TP_RETIREMENT_BATCH_INPUTS, &members) && members == count;
    if (cursor->ok)
    {
        entry->partition = group;
        entry->target = fields[2];
        entry->allocator = fields[3];
        entry->metrics = fields[4];
        entry->first = contract->input_count;
        entry->count = count;
    }
    for (u32 member = 0; cursor->ok && member < count; member += 1)
    {
        char* input[2] = {0};
        u32 row = UINT32_MAX;
        char* line = bq_retirement_check_line(cursor, "input=");
        cursor->ok = cursor->ok && bq_retirement_check_fields(line, input, 2) &&
                     bq_retirement_check_decimal(input[0], plan->row_count - 1u, &row) &&
                     row == untimed->rows[first + member] && tp_retirement_metrics_leaf(input[1]) &&
                     strcmp(input[1], entry->metrics) && strcmp(input[1], ".") && strcmp(input[1], "..");
        for (u32 previous = 0; cursor->ok && previous < member; previous += 1)
            cursor->ok = strcmp(contract->artifacts[entry->first + previous], input[1]) != 0;
        if (cursor->ok)
        {
            contract->artifacts[contract->input_count] = input[1];
            contract->rows[contract->input_count] = row;
            contract->input_count += 1;
        }
    }
}

/* The pinned contract for this attempt's row plan and untimed partition:
 * exactly one entry per untimed object group, in partition order, each
 * member the partition's. Refused before anything runs: no pin
 * (BQ_RECIPE_MISMATCH), an unreadable or unowned file
 * (BQ_CONFIGURATION_MISMATCH), or any other bytes (BQ_RECIPE_MISMATCH). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_untimed_import(Arena* arena, int installed, String8 profile,
    BqRetirementRowPlan const* plan, BqRetirementDocumentPartition const* untimed,
    BqRetirementWorkerUntimedContract* contract)
{
    char pin[SHA256_HEX_CAPACITY] = {0};
    BqError result = arena && installed >= 0 && plan && plan->owned && untimed && contract &&
                     bq_retirement_profile_sha(profile, S8(BQ_RETIREMENT_WORKER_UNTIMED_PIN), pin) ?
                     BQ_OK : BQ_RECIPE_MISMATCH;
    if (contract) *contract = (BqRetirementWorkerUntimedContract){0};
    int recipes = result == BQ_OK ? openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    u8* bytes = NULL;
    u32 length = 0;
    if (result == BQ_OK)
        result = recipes >= 0 && bq_owned_directory(recipes, false, true) &&
                 bq_retirement_reference_read_installed(recipes, BQ_RETIREMENT_WORKER_UNTIMED_NAME,
                     BQ_RETIREMENT_WORKER_UNTIMED_BYTES_CAP, &bytes, &length, contract->authority_sha256, NULL) ?
                 BQ_OK : BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK && memcmp(contract->authority_sha256, pin, SHA256_HEX_CAPACITY)) result = BQ_RECIPE_MISMATCH;
    char* text = result == BQ_OK ? bq_retirement_worker_allocate(arena, (u64)length + 1u, 1) : NULL;
    if (result == BQ_OK)
    {
        contract->groups = bq_retirement_worker_allocate(arena, untimed->count + 1u, sizeof(*contract->groups));
        contract->artifacts = bq_retirement_worker_allocate(arena, untimed->row_count + 1u,
                                                            sizeof(*contract->artifacts));
        contract->rows = bq_retirement_worker_allocate(arena, untimed->row_count + 1u, sizeof(*contract->rows));
        if (!text || !contract->groups || !contract->artifacts || !contract->rows) result = BQ_IO;
    }
    if (result == BQ_OK)
    {
        memcpy(text, bytes, length);
        for (u32 index = 0; index < length; index += 1)
            if (text[index] == '\n') text[index] = 0;
        BqRetirementCheckCursor cursor = {{(char8*)bytes, length}, text, 0, memchr(bytes, 0, length) == NULL};
        String8 line = {0};
        cursor.ok = cursor.ok && bq_next_line(cursor.bytes, &cursor.offset, &line) &&
                    string_equal(line, S8(BQ_RETIREMENT_WORKER_UNTIMED_HEADER));
        bq_retirement_check_digest_line(&cursor, "row-plan=", plan->authority_sha256);
        u32 groups = bq_retirement_check_count(&cursor, "groups=", 0, untimed->count);
        cursor.ok = cursor.ok && groups == untimed->object_groups;
        for (u32 group = 0; cursor.ok && group < untimed->count; group += 1)
        {
            if (!untimed->object[group]) continue;
            bq_retirement_worker_untimed_group(&cursor, plan, untimed, group, contract,
                                               contract->groups + contract->group_count);
            contract->group_count += cursor.ok;
        }
        cursor.ok = cursor.ok && cursor.offset == cursor.bytes.length;
        if (!cursor.ok) result = BQ_RECIPE_MISMATCH;
    }
    free(bytes);
    if (recipes >= 0 && close(recipes) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    if (result != BQ_OK && contract) *contract = (BqRetirementWorkerUntimedContract){0};
    return result;
}

/* --------------------------------------------------------- untimed batches */

/* The untimed step's caller data (lane D's retirement_untimed.h batches and
 * the review's untimed fields), in validator untimed-partition order: per
 * group and variant one command, run as the production and then the
 * reproduction batch. blocks are the commands' allocations
 * (bq_retirement_campaign_plan_command_copy); release frees them. */
typedef struct BqRetirementWorkerUntimed
{
    TpRetirementUntimedBatch* batches;
    TpRetirementBatchContract* contracts;
    TpRetirementBatchInput* inputs;
    TpRetirementMeasuredCommand* commands;
    char** blocks;
    unsigned* rows;
    unsigned* input_counts;
    unsigned* kinds;
    unsigned* stages;
    unsigned groups, object_groups;
    uint64_t metrics_bytes;
} BqRetirementWorkerUntimed;

BUSTER_GLOBAL_LOCAL void bq_retirement_worker_untimed_release(BqRetirementWorkerUntimed* untimed)
{
    for (u32 index = 0; untimed && untimed->blocks && index < untimed->groups * 2u; index += 1) free(untimed->blocks[index]);
    if (untimed) *untimed = (BqRetirementWorkerUntimed){0};
}

/* The response file of an object contract in the work directory, written
 * once (its leaf is its own digest; a launch rereads it). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_input_list(int work, TpRetirementBatchContract const* contract)
{
    char leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP];
    struct stat info = {0};
    bool present = tp_retirement_batch_input_list_leaf(contract, leaf) &&
                   fstatat(work, leaf, &info, AT_SYMLINK_NOFOLLOW) == 0;
    bool ok = present ? S_ISREG(info.st_mode) : tp_retirement_batch_input_list_write(work, contract, leaf) != 0;
    return ok;
}

/* One untimed command: the row plan's template resolved in the canonical
 * child layout for the variant's own binary, label 1, copied into its own
 * allocation and hashed. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_untimed_command(BqRetirementRowTemplate const* template,
    BqRetirementRowContext const* context, char const* artifact, char* storage, TpRetirementMeasuredCommand* command,
    char** block)
{
    BqRetirementRowCommand resolved;
    char* digests = NULL;
    bool ok = bq_retirement_row_command(template, context, storage, &resolved) &&
              bq_retirement_campaign_plan_command_copy(&resolved, artifact, command, block, &digests);
    if (ok)
    {
        command->directory = BQ_RETIREMENT_ROW_WORK_PATH;
        command->timeout_seconds = template->timeout_seconds;
        command->memory_mib = template->memory_mib;
    }
    return ok;
}

/* The untimed batches of every untimed group from the row plan the gate was
 * issued on and the pinned contract: a singleton runs its row's compile
 * template (whose label-1 digest must be the one the gate sealed) and must
 * reproduce the gate's artifact; an object group runs its contract template
 * over its members, each expected to reproduce the gate's artifact and
 * diagnostic, with the reviewed budget's metrics bound. Response files are
 * written into the work directory, except with BQ_RETIREMENT_WORKER_DERIVE_ONLY
 * (the coordinator's derivation of the execution plan, which launches
 * nothing). */
#define BQ_RETIREMENT_WORKER_DERIVE_ONLY (-1)
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_untimed_build(Arena* arena, int work, BqRetirementRowPlan const* plan,
    BqRetirementCorrectness const* gate, BqRetirementDocumentPartition const* partition,
    BqRetirementWorkerUntimedContract const* contract, TpRetirementCampaignBudget const* budget,
    BqRetirementWorkerUntimed* untimed)
{
    u32 groups = partition ? partition->count : 0, rows = partition ? partition->row_count : 0;
    *untimed = (BqRetirementWorkerUntimed){0};
    bool ok = arena && (work >= 3 || work == BQ_RETIREMENT_WORKER_DERIVE_ONLY) && plan && gate && contract && budget &&
              groups && groups <= TP_RETIREMENT_MAX_CELLS && contract->group_count == partition->object_groups;
    if (ok)
    {
        untimed->batches = bq_retirement_worker_allocate(arena, 4u * groups, sizeof(*untimed->batches));
        untimed->contracts = bq_retirement_worker_allocate(arena, 2u * groups, sizeof(*untimed->contracts));
        untimed->inputs = bq_retirement_worker_allocate(arena, 2u * rows + 1u, sizeof(*untimed->inputs));
        untimed->commands = bq_retirement_worker_allocate(arena, 2u * groups, sizeof(*untimed->commands));
        untimed->blocks = bq_retirement_worker_allocate(arena, 2u * groups, sizeof(*untimed->blocks));
        untimed->rows = bq_retirement_worker_allocate(arena, groups, sizeof(*untimed->rows));
        untimed->input_counts = bq_retirement_worker_allocate(arena, groups, sizeof(*untimed->input_counts));
        untimed->kinds = bq_retirement_worker_allocate(arena, groups, sizeof(*untimed->kinds));
        untimed->stages = bq_retirement_worker_allocate(arena, groups, sizeof(*untimed->stages));
        untimed->groups = groups;
        ok = untimed->batches && untimed->contracts && untimed->inputs && untimed->commands && untimed->blocks &&
             untimed->rows && untimed->input_counts && untimed->kinds && untimed->stages;
    }
    char* storage = ok ? bq_retirement_worker_allocate(arena, BQ_RETIREMENT_ROW_COMMAND_STORAGE, 1) : NULL;
    ok = ok && storage;
    u32 object = 0;
    for (u32 group = 0; ok && group < groups; group += 1)
    {
        u32 first = partition->first[group], members = partition->first[group + 1] - first;
        u32 leader = partition->rows[first];
        bool is_object = partition->object[group] != 0;
        BqRetirementWorkerUntimedGroup const* entry = is_object ? contract->groups + object : NULL;
        BqRetirementRowPlanRow const* planned = leader < plan->row_count ? plan->rows + leader : NULL;
        u64 bound = 0;
        ok = planned && leader < gate->prepared.rows && (is_object ?
             object < contract->group_count && entry->partition == group && entry->count == members &&
             tp_retirement_budget_metrics_bytes(budget, members, &bound) :
             members == 1 && planned->compile < plan->template_count && planned->fixture &&
             plan->templates[planned->compile].kind == BQ_RETIREMENT_ROW_TEMPLATE_COMPILE);
        u32 stage = gate->trusted_rows[ok ? leader : 0].stage;
        untimed->rows[group] = leader;
        untimed->input_counts[group] = members;
        untimed->kinds[group] = is_object ? TP_RETIREMENT_GROUP_OBJECT : TP_RETIREMENT_GROUP_SINGLETON;
        untimed->stages[group] = is_object ? TP_RETIREMENT_BUDGET_STAGE_OBJECT :
                                 stage == BQ_RETIREMENT_STAGE_SELF_HOST ? TP_RETIREMENT_BUDGET_STAGE_SELF_HOST :
                                 TP_RETIREMENT_BUDGET_STAGE_LINK;
        ok = ok && (is_object || stage == BQ_RETIREMENT_STAGE_LINK || stage == BQ_RETIREMENT_STAGE_SELF_HOST);
        for (u32 variant = 0; ok && variant < 2; variant += 1)
        {
            TpRetirementMeasuredCommand* command = untimed->commands + group * 2u + variant;
            TpRetirementBatchContract* batch = untimed->contracts + group * 2u + variant;
            char output[32], metrics[32], leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP];
            bq_retirement_row_leaves(leader, output, metrics);
            if (is_object)
            {
                TpRetirementBatchInput* inputs = untimed->inputs + (size_t)variant * rows + first;
                for (u32 member = 0; ok && member < members; member += 1)
                {
                    u32 row = partition->rows[first + member];
                    BqRetirementObservedSide const* side = &gate->facts[row].side[variant];
                    ok = row < plan->row_count && plan->rows[row].fixture && tp_retirement_digest(side->artifact_sha256) &&
                         tp_retirement_digest(side->diagnostic_sha256) &&
                         contract->rows[entry->first + member] == row;
                    if (ok)
                        inputs[member] = (TpRetirementBatchInput){plan->rows[row].fixture, "ok", "driver.none",
                            side->diagnostic_sha256, side->artifact_sha256, contract->artifacts[entry->first + member],
                            1, row};
                }
                *batch = (TpRetirementBatchContract){entry->target, entry->allocator, entry->metrics, inputs, members,
                                                     0, bound};
                ok = ok && tp_retirement_batch_contract_valid(batch) && tp_retirement_batch_input_list_leaf(batch, leaf) &&
                     (work == BQ_RETIREMENT_WORKER_DERIVE_ONLY || bq_retirement_worker_input_list(work, batch));
                BqRetirementRowContext context = {.metrics = entry->metrics, .inputs = leaf, .side = variant,
                                                  .label = 1};
                ok = ok && bq_retirement_worker_untimed_command(plan->templates + entry->template_index, &context, "",
                                                                storage, command, untimed->blocks + group * 2u + variant);
                if (ok)
                {
                    command->batch = batch;
                    ok = tp_retirement_batch_contract_output(batch, (char*)command->output_sha256);
                }
            }
            else
            {
                BqRetirementRowContext context = {.fixture = planned->fixture, .output = output, .metrics = metrics,
                                                  .side = variant, .label = 1};
                char const* objects[1] = {gate->facts[leader].side[variant].artifact_sha256};
                ok = tp_retirement_digest(objects[0]) &&
                     bq_retirement_worker_untimed_command(plan->templates + planned->compile, &context, output, storage,
                                                          command, untimed->blocks + group * 2u + variant) &&
                     tp_retirement_batch_output_digest(objects, 1, (char*)command->output_sha256);
            }
            if (ok)
            {
                command->unit = group;
                command->kind = 0;
                command->variant = variant;
                command->exit_status = 0;
                ok = tp_retirement_command_hash(command, (char*)command->command_sha256) &&
                     (is_object || !strcmp(command->command_sha256,
                                           gate->trusted_rows[leader].compiler_command_sha256[variant]));
            }
            for (u32 purpose = 0; ok && purpose < 2; purpose += 1)
                untimed->batches[(group * 2u + variant) * 2u + purpose] = (TpRetirementUntimedBatch){*command, group,
                    variant, purpose, untimed->kinds[group]};
        }
        if (ok && is_object)
        {
            object += 1;
            untimed->object_groups += 1;
            ok = bound <= UINT64_MAX / 4u && untimed->metrics_bytes <= UINT64_MAX - 4u * bound;
            if (ok) untimed->metrics_bytes += 4u * bound;
        }
    }
    ok = ok && object == contract->group_count;
    return ok;
}

/* ------------------------------------------------------------ timed layout */

/* The frozen A1 sample layout of the timed projection (TpRetirementLayout),
 * the runtime rows and each group's budget stage, from the sealed gate: the
 * timed rows in ascending order (dense indexes), runtime for the singletons
 * the gate observed a native runtime obligation for; the groups in the
 * campaign's order (bq_retirement_campaign_plan_commands'), each frozen
 * object group at its first member with its members in contract order, each
 * timed singleton at its row. */
typedef struct BqRetirementWorkerLayout
{
    unsigned* row_ids;
    unsigned* row_metrics;
    unsigned* kinds;
    unsigned* offsets;
    unsigned* members;
    unsigned* runtime_rows;
    unsigned* group_stages;
    unsigned rows, groups, runtime_count, objects;
} BqRetirementWorkerLayout;

BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_layout_build(Arena* arena, BqRetirementCorrectness const* gate,
    BqRetirementWorkerLayout* layout)
{
    u32 timed = gate ? bq_retirement_campaign_timed_rows(gate) : 0;
    u32 groups = gate ? bq_retirement_campaign_timed_groups(gate) : UINT32_MAX;
    *layout = (BqRetirementWorkerLayout){0};
    bool ok = arena && gate && timed && timed <= TP_RETIREMENT_MAX_CELLS && groups != UINT32_MAX && groups &&
              groups <= timed;
    if (ok)
    {
        layout->row_ids = bq_retirement_worker_allocate(arena, timed, sizeof(unsigned));
        layout->row_metrics = bq_retirement_worker_allocate(arena, timed, sizeof(unsigned));
        layout->members = bq_retirement_worker_allocate(arena, timed, sizeof(unsigned));
        layout->runtime_rows = bq_retirement_worker_allocate(arena, timed, sizeof(unsigned));
        layout->kinds = bq_retirement_worker_allocate(arena, groups, sizeof(unsigned));
        layout->group_stages = bq_retirement_worker_allocate(arena, groups, sizeof(unsigned));
        layout->offsets = bq_retirement_worker_allocate(arena, groups + 1u, sizeof(unsigned));
        ok = layout->row_ids && layout->row_metrics && layout->members && layout->runtime_rows && layout->kinds &&
             layout->group_stages && layout->offsets;
    }
    u32 native = ok ? gate->prepared.native_target : 0;
    for (u32 row = 0; ok && row < gate->prepared.rows; row += 1)
    {
        BqRetirementTrustedRow const* trusted = gate->trusted_rows + row;
        bool is_timed = trusted->compiler_eligible && trusted->target == native;
        bool runtime = is_timed && trusted->stage != BQ_RETIREMENT_STAGE_OBJECT && gate->facts[row].runtime_eligible;
        if (is_timed)
        {
            layout->row_ids[layout->rows] = row;
            layout->row_metrics[layout->rows] = runtime ? TP_RETIREMENT_SAMPLE_RUNTIME : 0;
            layout->rows += 1;
        }
        if (runtime) layout->runtime_rows[layout->runtime_count++] = row;
    }
    u32 used = 0, next_batch = 0;
    for (u32 dense = 0; ok && dense < layout->rows; dense += 1)
    {
        u32 row = layout->row_ids[dense];
        u32 stage = gate->trusted_rows[row].stage;
        bool object = stage == BQ_RETIREMENT_STAGE_OBJECT;
        BqRetirementBatchGroup const* frozen = object && next_batch < gate->batch_group_count ?
                                               gate->batch_groups + next_batch : NULL;
        if (object && bq_retirement_campaign_plan_first_member(frozen) != row) continue;
        u32 group = layout->groups;
        ok = group < groups && (!object || frozen);
        if (ok)
        {
            layout->groups += 1;
            layout->kinds[group] = object ? TP_RETIREMENT_GROUP_OBJECT : TP_RETIREMENT_GROUP_SINGLETON;
            layout->group_stages[group] = object ? TP_RETIREMENT_BUDGET_STAGE_OBJECT :
                                          stage == BQ_RETIREMENT_STAGE_LINK ? TP_RETIREMENT_BUDGET_STAGE_LINK :
                                          TP_RETIREMENT_BUDGET_STAGE_SELF_HOST;
            layout->offsets[group] = used;
            ok = object || stage == BQ_RETIREMENT_STAGE_LINK || stage == BQ_RETIREMENT_STAGE_SELF_HOST;
            if (ok && !object) layout->members[used++] = dense;
        }
        for (u32 input = 0; ok && frozen && input < frozen->contract[0].input_count; input += 1)
        {
            if (!frozen->contract[0].inputs[input].member) continue;
            u32 member = 0;
            while (member < layout->rows && layout->row_ids[member] != frozen->contract[0].inputs[input].row) member += 1;
            ok = member < layout->rows && used < layout->rows;
            if (ok) layout->members[used++] = member;
        }
        next_batch += object;
        layout->objects += object;
    }
    if (ok) layout->offsets[layout->groups] = used;
    ok = ok && used == layout->rows && layout->groups == groups && next_batch == gate->batch_group_count &&
         layout->runtime_count;
    return ok;
}

/* ------------------------------------------------------------------ stages */

/* One timed stage's collector state and its arena storage: the execution
 * cursor's 3G + U workspace, the sample rows, groups and members, and the
 * private sample spool in the scratch directory. */
typedef struct BqRetirementWorkerStage
{
    TpRetirementExecution execution;
    TpRetirementTranscript transcript;
    TpRetirementSamples samples;
    TpRetirementMetricsShards metrics;
    TpRetirementSampleRow* rows;
    TpRetirementSampleGroup* groups;
    unsigned* members;
    unsigned* workspace;
    FILE* spool;
} BqRetirementWorkerStage;

/* ----------------------------------------------------------------- streams */

/* A set of staged streams of one kind, in index order: each a new private
 * file in the streams directory named by its store path. */
typedef struct BqRetirementWorkerStreamGroup
{
    FILE** streams;
    char (*paths)[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    unsigned count;
} BqRetirementWorkerStreamGroup;

typedef enum BqRetirementWorkerStreamKind
{
    BQ_RETIREMENT_WORKER_STREAM_TRANSCRIPT,
    BQ_RETIREMENT_WORKER_STREAM_METRICS,
    BQ_RETIREMENT_WORKER_STREAM_SAMPLES,
    BQ_RETIREMENT_WORKER_STREAM_UNTIMED,
    BQ_RETIREMENT_WORKER_STREAM_RECORD
} BqRetirementWorkerStreamKind;

/* Store path `index` of a kind: transcripts `retirement-execution-<tag>-NNNN.jsonl`,
 * metrics shards the writer's own `retirement-metrics-<tag>-NNNN.txt`, sample
 * shards the row population's `retirement-samples-<prefix>NNNN.jsonl` for
 * the first `rows` then `retirement-batches-<prefix>NNNN.jsonl`, the untimed
 * records and the post-sample record their fixed paths. Zero-padded indexes
 * sort in index order. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_stream_path(char path[TP_RETIREMENT_STORE_PATH_BYTES + 1],
    BqRetirementWorkerStreamKind kind, unsigned stage, char const* tag, unsigned index, unsigned rows)
{
    int length = -1;
    if (kind == BQ_RETIREMENT_WORKER_STREAM_TRANSCRIPT && stage < TP_RETIREMENT_CAMPAIGN_STAGES)
        length = snprintf(path, TP_RETIREMENT_STORE_PATH_BYTES + 1, "retirement-execution-%s-%04u.jsonl",
                          bq_retirement_worker_tags[stage], index);
    else if (kind == BQ_RETIREMENT_WORKER_STREAM_METRICS)
        length = tp_retirement_metrics_shard_path(path, tag, index) ? (int)strlen(path) : -1;
    else if (kind == BQ_RETIREMENT_WORKER_STREAM_SAMPLES && stage < TP_RETIREMENT_CAMPAIGN_STAGES)
        length = snprintf(path, TP_RETIREMENT_STORE_PATH_BYTES + 1, "retirement-%s-%s%04u.jsonl",
                          index < rows ? "samples" : "batches", bq_retirement_worker_sample_tags[stage],
                          index < rows ? index : index - rows);
    else if (kind == BQ_RETIREMENT_WORKER_STREAM_UNTIMED && !index)
        length = snprintf(path, TP_RETIREMENT_STORE_PATH_BYTES + 1, "%s", BQ_RETIREMENT_WORKER_UNTIMED_PATH);
    else if (kind == BQ_RETIREMENT_WORKER_STREAM_RECORD && !index)
        length = snprintf(path, TP_RETIREMENT_STORE_PATH_BYTES + 1, "%s", BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD);
    bool ok = length > 0 && length <= (int)TP_RETIREMENT_STORE_PATH_BYTES;
    return ok;
}

/* `count` new, empty staged streams of one kind with indexes from `first`
 * (paths in index order), refused beyond the attempt's stream cap (*opened
 * counts every stream the attempt staged). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_streams_open(Arena* arena, int directory,
    BqRetirementWorkerStreamKind kind, unsigned stage, char const* tag, unsigned first, u64 count, unsigned rows,
    unsigned* opened, BqRetirementWorkerStreamGroup* group)
{
    *group = (BqRetirementWorkerStreamGroup){0};
    bool ok = arena && directory >= 3 && count && count <= BQ_RETIREMENT_WORKER_STREAMS_MAX &&
              *opened <= BQ_RETIREMENT_WORKER_STREAMS_MAX - count && first <= BQ_RETIREMENT_WORKER_STREAMS_MAX;
    if (ok)
    {
        group->streams = bq_retirement_worker_allocate(arena, count, sizeof(*group->streams));
        group->paths = bq_retirement_worker_allocate(arena, count, sizeof(*group->paths));
        ok = group->streams && group->paths;
    }
    for (u32 index = 0; ok && index < count; index += 1)
    {
        ok = bq_retirement_worker_stream_path(group->paths[index], kind, stage, tag, first + index, rows);
        int file = ok ? openat(directory, group->paths[index], O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) :
                   -1;
        group->streams[index] = file >= 0 ? fdopen(file, "w+b") : NULL;
        if (file >= 0 && !group->streams[index]) close(file);
        ok = group->streams[index] != NULL;
        if (ok)
        {
            group->count += 1;
            *opened += 1;
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_streams_close(BqRetirementWorkerStreamGroup* group)
{
    bool ok = true;
    for (u32 index = 0; group && group->streams && index < group->count; index += 1)
        if (group->streams[index] && fclose(group->streams[index]) != 0) ok = false;
    if (group) *group = (BqRetirementWorkerStreamGroup){0};
    return ok;
}

/* Publish the first `used` streams of a group into the result store, in
 * index order: each is copied into a new store file while hashed and sealed
 * with that digest (tp_retirement_store_publish rereads it). A failure
 * aborts the pending file and poisons the store. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_streams_publish(TpRetirementStore* store,
    BqRetirementWorkerStreamGroup const* group, unsigned used)
{
    bool ok = store && group && used <= group->count;
    unsigned char buffer[65536];
    for (u32 index = 0; ok && index < used; index += 1)
    {
        FILE* source = group->streams[index];
        TpRetirementPending pending = {0};
        ok = fflush(source) == 0 && fseek(source, 0, SEEK_SET) == 0 &&
             tp_retirement_store_begin(store, group->paths[index], TP_RETIREMENT_STORE_FILE_BYTES, &pending);
        Sha256 hash;
        sha256_init(&hash);
        u64 bytes = 0;
        size_t count = 0;
        while (ok && (count = fread(buffer, 1, sizeof(buffer), source)) > 0)
        {
            ok = fwrite(buffer, 1, count, pending.stream) == count && bytes <= UINT64_MAX - count;
            sha256_add(&hash, buffer, (u64)count);
            bytes += count;
        }
        ok = ok && !ferror(source);
        char digest[SHA256_HEX_CAPACITY];
        sha256_finish_hex(&hash, (char8*)digest);
        if (ok) ok = tp_retirement_store_publish(store, &pending, bytes, digest) != 0;
        else if (pending.stream) tp_retirement_store_abort(store, &pending);
    }
    return ok;
}

/* -------------------------------------------------------------- store plan */

/* Lane E's pre-timing store plan in the result root: both stages'
 * capacity, the composer's outputs, the retained declaration (the A/A
 * transcript, sample and metrics shards, which the capacity reserves, and
 * D's two constant entries, bq_retirement_unit_handoff_declared), D's five
 * documents as prior entries at their measured sizes, and the result root's
 * worker-written entries (BQ_RETIREMENT_WORKER_RESULT_ENTRIES: the control
 * files and the phase receipts, each at the per-file cap, and the binding
 * context's `evidence_entries` evidence files, `evidence_bytes` in all) as
 * external entries, so every store file plus every such entry stays within
 * BQ_WORKER_BUNDLE_ENTRY_CAP; any excess refuses. */
#define BQ_RETIREMENT_WORKER_RETAINED (4u + BQ_RETIREMENT_UNIT_HANDOFF_RETAINED)
typedef struct BqRetirementWorkerDeclaration
{
    TpRetirementComposeRetained retained[BQ_RETIREMENT_WORKER_RETAINED];
    TpRetirementComposeDeclaration declaration;
} BqRetirementWorkerDeclaration;

BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_declaration(TpRetirementCampaignCapacity const* capacity,
    uint64_t const document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS], BqRetirementWorkerDeclaration* out)
{
    u64 rows = tp_retirement_campaign_ceil_div(capacity->row_samples_per_stage, TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
    u64 batches = capacity->sample_shards_per_stage >= rows ? capacity->sample_shards_per_stage - rows : 0;
    u64 prior = 0;
    bool ok = rows && batches && capacity->transcript_shards_per_stage && capacity->metrics_shards_per_stage_upper_bound &&
              capacity->transcript_shards_per_stage <= TP_RETIREMENT_COMPOSE_GROUP_FILES &&
              rows <= TP_RETIREMENT_COMPOSE_GROUP_FILES && batches <= TP_RETIREMENT_COMPOSE_GROUP_FILES &&
              capacity->metrics_shards_per_stage_upper_bound <= TP_RETIREMENT_COMPOSE_GROUP_FILES;
    for (u32 index = 0; ok && index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS; index += 1)
    {
        ok = document_bytes[index] && prior <= UINT64_MAX - document_bytes[index];
        if (ok) prior += document_bytes[index];
    }
    if (ok)
    {
        out->retained[0] = (TpRetirementComposeRetained){"transcript", "retirement-execution-aa-", ".jsonl",
            (unsigned)capacity->transcript_shards_per_stage, 1, 0};
        out->retained[1] = (TpRetirementComposeRetained){"samples", "retirement-samples-aa-", ".jsonl",
            (unsigned)rows, 1, 0};
        out->retained[2] = (TpRetirementComposeRetained){"batches", "retirement-batches-aa-", ".jsonl",
            (unsigned)batches, 1, 0};
        out->retained[3] = (TpRetirementComposeRetained){"metrics", "retirement-metrics-aa-", ".txt",
            (unsigned)capacity->metrics_shards_per_stage_upper_bound, 1, 0};
        for (u32 index = 0; index < BQ_RETIREMENT_UNIT_HANDOFF_RETAINED; index += 1)
            out->retained[4 + index] = bq_retirement_unit_handoff_declared[index];
        out->declaration = (TpRetirementComposeDeclaration){out->retained, BQ_RETIREMENT_WORKER_RETAINED,
            BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS, prior};
    }
    return ok;
}

/* The campaign's timed rows for lane E (dimension values from the pinned
 * rows) and the store plan over them. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_store_plan(TpRetirementStore* store,
    TpRetirementCampaignCapacity const* capacity, TpRetirementComposeLayout const* layout, unsigned pairs,
    unsigned code_rows, BqRetirementWorkerDeclaration const* declared, u32 evidence_entries, u64 evidence_bytes,
    TpRetirementCampaignStorePlan* plan, TpRetirementFamilyCounts* family)
{
    TpRetirementComposeShape shape = {layout, pairs, code_rows, BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS};
    TpRetirementComposeBounds bounds = {0};
    u32 externals = BQ_RETIREMENT_WORKER_RESULT_ENTRIES + evidence_entries;
    bool ok = evidence_entries <= BQ_RETIREMENT_WORKER_EVIDENCE_CAP &&
              evidence_bytes <= (u64)evidence_entries * BQ_WORKER_BUNDLE_FILE_CAP &&
              tp_retirement_compose_bounds(&shape, &bounds) &&
              tp_retirement_compose_plan(store, capacity, &shape, &declared->declaration, externals,
                  BQ_RETIREMENT_WORKER_CONTROL_BYTES + evidence_bytes, plan) &&
              plan->external_entries >= externals && plan->entries <= BQ_WORKER_BUNDLE_ENTRY_CAP;
    *family = ok ? (TpRetirementFamilyCounts){bounds.bootstrap_members, bounds.cell_members} :
                   (TpRetirementFamilyCounts){0};
    return ok;
}

/* ---------------------------------------------------------------- campaign */

typedef struct BqRetirementWorkerCampaign
{
    Arena* arena;
    BqRetirementUnitCampaign driver;
    BqRetirementHeldBinaries held;
    BqRetirementCampaignReady ready;
    BqRetirementRowPlan row_plan;
    BqRetirementCampaignPlanCommands commands;
    BqRetirementDocumentPopulation population;
    BqRetirementDocumentPartition partition;
    BqRetirementUnitCampaignPins pins;
    TpRetirementCampaignBudget budget;
    BqRetirementWorkerUntimedContract contract;
    BqRetirementWorkerUntimed untimed_build;
    TpRetirementUntimed untimed;
    TpRetirementMetricsShards untimed_metrics;
    unsigned char* reproduced;
    TpRetirementCodeRow* codes;
    unsigned code_capacity;
    BqRetirementWorkerLayout layout;
    BqRetirementWorkerStage stages[TP_RETIREMENT_CAMPAIGN_STAGES];
    /* Staged streams: the untimed records and metrics shards, per stage the
     * first transcript and metrics shard (needed before the bind) and the
     * spares and sample shards the frozen capacity sizes, and the
     * post-sample record. */
    BqRetirementWorkerStreamGroup untimed_records, untimed_shards, record;
    BqRetirementWorkerStreamGroup transcripts_first[TP_RETIREMENT_CAMPAIGN_STAGES];
    BqRetirementWorkerStreamGroup transcripts[TP_RETIREMENT_CAMPAIGN_STAGES];
    BqRetirementWorkerStreamGroup metrics_first[TP_RETIREMENT_CAMPAIGN_STAGES];
    BqRetirementWorkerStreamGroup metrics[TP_RETIREMENT_CAMPAIGN_STAGES];
    BqRetirementWorkerStreamGroup samples[TP_RETIREMENT_CAMPAIGN_STAGES];
    TpRetirementCampaignReview review;
    unsigned streams_opened;
    TpRetirementStore store;
    TpRetirementStoredFile* store_files;
    TpRetirementCampaign campaign;
    BqRetirementCampaignBinding binding;
    TpRetirementCampaignCommand* snapshots;
    unsigned* identities;
    TpRetirementPlan plan;
    char plan_sha256[SHA256_HEX_CAPACITY], context_sha256[SHA256_HEX_CAPACITY];
    TpRetirementTimedRow* timed_rows;
    TpRetirementComposeRow* compose_rows;
    TpRetirementComposeLayout compose_layout;
    BqRetirementWorkerDeclaration declaration;
    TpRetirementCampaignStorePlan store_plan;
    TpRetirementFamilyCounts family;
    uint64_t document_bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS];
    /* The binding context's evidence files, reserved in the store plan and
     * published at composition (bq_retirement_worker_evidence_publish). */
    u64 evidence_bytes;
    u32 evidence_entries;
    /* The A/A admission receipt the admission step verified (fixture only:
     * production admission is compiled out), which the binding names. */
    char aa_receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX];
    u32 aa_receipt_bytes;
    int attempt, directory, directories[BQ_RETIREMENT_WORKER_DIRECTORIES];
    /* composed: the authority is issued over the store, which is then
     * closed rather than aborted. */
    bool begun, composed;
} BqRetirementWorkerCampaign;

/* The count and total bytes of the evidence files the pinned binding context
 * names (retirement_worker_compose.c), which the store plan reserves. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_evidence_measure(Arena* arena, int installed, String8 profile,
    u32* entries, u64* bytes);

/* Composition, the authority and MEASURED (retirement_worker_compose.c). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_compose(BqRetirementWorkerCampaign* campaign,
    BqRetirementCampaignUnitStore const* unit, BqJob const* job, BqRetirementUnitGate const* unit_gate, int result_root,
    char const* result_path, char const* adapter, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const ready_sha256[SHA256_HEX_CAPACITY], u64 deadline_ns);

/* The attempt's new, private campaign directory and its sub-directories. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_directories_open(BqRetirementWorkerCampaign* campaign, int workspaces,
    BqJob const* job)
{
    char attempt_name[64];
    bool created = false;
    campaign->attempt = bq_workspace_name(attempt_name, job->id, job->token) ?
                        openat(workspaces, attempt_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    campaign->directory = campaign->attempt >= 0 && bq_workspace_seal(campaign->attempt, job, false) ?
                          bq_retirement_unit_promote(bq_create_inherited_group_directory(campaign->attempt,
                              BQ_RETIREMENT_WORKER_CAMPAIGN_DIRECTORY, 02700, &created)) : -1;
    BqError result = campaign->directory >= 3 && created && fsync(campaign->attempt) == 0 ? BQ_OK : BQ_WORKSPACE_MISMATCH;
    for (u32 index = 0; result == BQ_OK && index < BQ_RETIREMENT_WORKER_DIRECTORIES; index += 1)
    {
        campaign->directories[index] = mkdirat(campaign->directory, bq_retirement_worker_directories[index], 0700) == 0 ?
            bq_retirement_unit_promote(openat(campaign->directory, bq_retirement_worker_directories[index],
                                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) : -1;
        if (!(campaign->directories[index] >= 3 && bq_owned_directory(campaign->directories[index], true, false)))
            result = BQ_WORKSPACE_MISMATCH;
    }
    if (result == BQ_OK && fsync(campaign->directory) != 0) result = BQ_IO;
    return result;
}

/* This boot's identifier, the transcripts' `boot` token. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_boot(char boot[TP_RETIREMENT_STORE_TOKEN_CAPACITY])
{
    int file = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t count = file >= 0 ? read(file, boot, TP_RETIREMENT_STORE_TOKEN_CAPACITY - 1) : -1;
    bool ok = count > 1 && boot[count - 1] == '\n';
    if (ok) boot[count - 1] = 0;
    if (file >= 0 && close(file) != 0) ok = false;
    ok = ok && tp_retirement_token(boot);
    if (!ok) boot[0] = 0;
    return ok;
}

/* SETTLING: the driver's acknowledgement, the store-based import, the row
 * plan, the reviewed budget and pinned rows, the untimed batches and the
 * untimed step. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_campaign_settle(BqRetirementWorkerCampaign* campaign,
    BqRetirementCampaignUnitStore const* unit, BqPhaseChannel* phases, int cancellation_fd, u64 deadline_ns,
    BqJob const* job, BqRetirementProjection const* projection, BqRetirementUnitGate const* unit_gate,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const ready_sha256[SHA256_HEX_CAPACITY])
{
    BqRetirementCorrectness const* gate = &unit_gate->correctness;
    int* directories = campaign->directories;
    BqError result = bq_retirement_unit_campaign_begin(&campaign->driver, phases, cancellation_fd, deadline_ns,
        directories[BQ_RETIREMENT_WORKER_WORK], directories[BQ_RETIREMENT_WORKER_LOGS]) ? BQ_OK : BQ_WORKER_MISMATCH;
    campaign->begun = true;
    if (result == BQ_OK)
        result = bq_retirement_campaign_service_import_unit_pinned(unit, phases, cancellation_fd, deadline_ns, job->id,
            job->token, preparation_sha256, ready_sha256, unit_gate, &campaign->held, &campaign->ready);
    if (result == BQ_OK)
        result = bq_retirement_row_plan_import_profile(unit->installed, unit->profile, job, projection,
                                                       &campaign->row_plan);
    if (result == BQ_OK && !bq_retirement_unit_campaign_pins(unit->profile, &campaign->pins)) result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_worker_budget_load(unit->installed, unit->profile, &campaign->budget);
    if (result == BQ_OK)
    {
        char budget_sha256[SHA256_HEX_CAPACITY] = {0};
        if (!tp_retirement_budget_digest(&campaign->budget, budget_sha256) ||
            strcmp(budget_sha256, campaign->pins.budget_sha256))
            result = BQ_RECIPE_MISMATCH;
    }
    if (result == BQ_OK)
        result = bq_retirement_documents_population(unit->installed, unit->profile, gate->trusted_rows,
            gate->prepared.rows, gate->prepared.native_target, &campaign->population);
    if (result == BQ_OK && !bq_retirement_documents_partition(&campaign->population, 1, &campaign->partition))
        result = BQ_IO;
    if (result == BQ_OK)
        result = bq_retirement_worker_untimed_import(campaign->arena, unit->installed, unit->profile,
                                                     &campaign->row_plan, &campaign->partition, &campaign->contract);
    if (result == BQ_OK &&
        !bq_retirement_worker_untimed_build(campaign->arena, directories[BQ_RETIREMENT_WORKER_WORK], &campaign->row_plan,
                                            gate, &campaign->partition, &campaign->contract, &campaign->budget,
                                            &campaign->untimed_build))
        result = BQ_RECIPE_MISMATCH;
    BqRetirementWorkerUntimed const* build = &campaign->untimed_build;
    /* Every code-observed row of the gate gets one code entry. */
    for (u32 row = 0; result == BQ_OK && row < gate->prepared.rows; row += 1)
        campaign->code_capacity += gate->trusted_rows[row].compiler_eligible ? 1u : 0u;
    u64 artifacts = 4u * (u64)build->object_groups;
    u64 spares = result == BQ_OK && build->object_groups ?
                 tp_retirement_campaign_metrics_shards(artifacts, build->metrics_bytes) : 0;
    if (result == BQ_OK)
    {
        campaign->codes = bq_retirement_worker_allocate(campaign->arena, campaign->code_capacity + 1u,
                                                        sizeof(*campaign->codes));
        campaign->reproduced = bq_retirement_worker_allocate(campaign->arena, 2u * build->groups, 1);
        int streams = directories[BQ_RETIREMENT_WORKER_STREAMS];
        if (!campaign->codes || !campaign->reproduced ||
            !bq_retirement_worker_streams_open(campaign->arena, streams, BQ_RETIREMENT_WORKER_STREAM_UNTIMED, 0, NULL, 0,
                                               1, 0, &campaign->streams_opened, &campaign->untimed_records) ||
            (build->object_groups &&
             !bq_retirement_worker_streams_open(campaign->arena, streams, BQ_RETIREMENT_WORKER_STREAM_METRICS, 0,
                                                TP_RETIREMENT_UNTIMED_METRICS_TAG, 0, spares, 0,
                                                &campaign->streams_opened, &campaign->untimed_shards)))
            result = BQ_IO;
    }
    char label[TP_RETIREMENT_STORE_TOKEN_CAPACITY] = {0}, boot[TP_RETIREMENT_STORE_TOKEN_CAPACITY] = {0};
    u64 reserved = bq_phase_clock();
    bool metered = build->object_groups != 0;
    if (result == BQ_OK &&
        !(bq_retirement_campaign_job_label(label, job->id) && bq_retirement_worker_boot(boot) &&
          (!metered || tp_retirement_metrics_shards_init(&campaign->untimed_metrics, TP_RETIREMENT_UNTIMED_METRICS_TAG,
                                                         campaign->untimed_shards.streams[0])) &&
          tp_retirement_untimed_init(&campaign->untimed, campaign->untimed_records.streams[0],
              metered ? &campaign->untimed_metrics : NULL, &campaign->budget, build->groups, campaign->reproduced,
              label, job->token, boot, (int)campaign->row_plan.cpu, reserved, deadline_ns, 0)))
        result = BQ_RECIPE_MISMATCH;
    campaign->review = (TpRetirementCampaignReview){&campaign->budget, NULL, build->input_counts, build->kinds,
                                                    build->stages, 0, build->groups};
    BqRetirementUnitCampaignStreams streams = {NULL, campaign->untimed_shards.count ?
                                               campaign->untimed_shards.streams + 1 : NULL, NULL, 0,
                                               campaign->untimed_shards.count ? campaign->untimed_shards.count - 1u : 0,
                                               0};
    BqRetirementUnitCampaignCode code = {build->rows, campaign->codes, campaign->code_capacity,
                                         directories[BQ_RETIREMENT_WORKER_CODE]};
    if (result == BQ_OK &&
        !bq_retirement_unit_campaign_untimed(&campaign->driver, &campaign->untimed, build->batches, 4u * build->groups,
                                             &campaign->held, campaign->ready.sources, gate, &campaign->review, &streams,
                                             &code))
        result = BQ_WORKER_FAILED;
    return result;
}

/* Both stages' collectors over the timed layout, bound at `bound_ns` (after
 * the MEASURING acknowledgement), each with its first transcript shard, its
 * first metrics shard (the layout has object groups) and a private spool. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_stages_open(BqRetirementWorkerCampaign* campaign, BqJob const* job,
    u64 bound_ns)
{
    BqRetirementWorkerLayout const* layout = &campaign->layout;
    TpRetirementLayout frozen = {layout->rows, layout->groups, layout->row_ids, layout->row_metrics, layout->kinds,
                                 layout->offsets, layout->members};
    char label[TP_RETIREMENT_STORE_TOKEN_CAPACITY] = {0}, boot[TP_RETIREMENT_STORE_TOKEN_CAPACITY] = {0};
    u64 slots = 3u * (u64)layout->groups + layout->runtime_count;
    bool ok = bq_retirement_campaign_job_label(label, job->id) && bq_retirement_worker_boot(boot) && layout->objects;
    int streams = campaign->directories[BQ_RETIREMENT_WORKER_STREAMS];
    for (u32 index = 0; ok && index < TP_RETIREMENT_CAMPAIGN_STAGES; index += 1)
    {
        BqRetirementWorkerStage* stage = campaign->stages + index;
        stage->rows = bq_retirement_worker_allocate(campaign->arena, layout->rows, sizeof(*stage->rows));
        stage->groups = bq_retirement_worker_allocate(campaign->arena, layout->groups, sizeof(*stage->groups));
        stage->members = bq_retirement_worker_allocate(campaign->arena, layout->rows, sizeof(*stage->members));
        stage->workspace = bq_retirement_worker_allocate(campaign->arena, slots, sizeof(*stage->workspace));
        char spool[16];
        snprintf(spool, sizeof(spool), "%s.spool", bq_retirement_worker_tags[index]);
        int file = openat(campaign->directories[BQ_RETIREMENT_WORKER_SCRATCH], spool,
                          O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        stage->spool = file >= 0 ? fdopen(file, "w+b") : NULL;
        if (file >= 0 && !stage->spool) close(file);
        ok = stage->rows && stage->groups && stage->members && stage->workspace && stage->spool &&
             bq_retirement_worker_streams_open(campaign->arena, streams, BQ_RETIREMENT_WORKER_STREAM_TRANSCRIPT, index,
                 NULL, 0, 1, 0, &campaign->streams_opened, &campaign->transcripts_first[index]) &&
             bq_retirement_worker_streams_open(campaign->arena, streams, BQ_RETIREMENT_WORKER_STREAM_METRICS, index,
                 bq_retirement_worker_tags[index], 0, 1, 0, &campaign->streams_opened, &campaign->metrics_first[index]) &&
             tp_retirement_execution_init(&stage->execution, campaign->pins.seed, layout->groups, layout->runtime_rows,
                 layout->runtime_count, campaign->ready.rows, campaign->pins.pairs, stage->workspace, slots) &&
             tp_retirement_transcript_init(&stage->transcript, &stage->execution, label, job->token, boot,
                 (int)campaign->row_plan.cpu, bound_ns) &&
             tp_retirement_transcript_begin_shard(&stage->transcript, campaign->transcripts_first[index].streams[0]) &&
             tp_retirement_samples_init(&stage->samples, &stage->transcript, stage->spool, &frozen, stage->rows,
                 stage->groups, stage->members) &&
             tp_retirement_metrics_shards_init(&stage->metrics, bq_retirement_worker_tags[index],
                 campaign->metrics_first[index].streams[0]) &&
             tp_retirement_samples_attach_metrics(&stage->samples, &stage->metrics);
    }
    return ok;
}

/* The spare transcript and metrics shards and the sample shards each stage
 * may need, as the frozen capacity bounds them. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_stage_streams(BqRetirementWorkerCampaign* campaign)
{
    TpRetirementCampaignCapacity const* capacity = &campaign->campaign.capacity;
    u64 rows = tp_retirement_campaign_ceil_div(capacity->row_samples_per_stage, TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
    int streams = campaign->directories[BQ_RETIREMENT_WORKER_STREAMS];
    bool ok = capacity->transcript_shards_per_stage && capacity->metrics_shards_per_stage_upper_bound &&
              capacity->sample_shards_per_stage && rows <= capacity->sample_shards_per_stage &&
              capacity->sample_shards_per_stage <= BQ_RETIREMENT_WORKER_STREAMS_MAX;
    for (u32 index = 0; ok && index < TP_RETIREMENT_CAMPAIGN_STAGES; index += 1)
        ok = (capacity->transcript_shards_per_stage == 1 ||
              bq_retirement_worker_streams_open(campaign->arena, streams, BQ_RETIREMENT_WORKER_STREAM_TRANSCRIPT, index,
                  NULL, 1, capacity->transcript_shards_per_stage - 1u, 0, &campaign->streams_opened,
                  &campaign->transcripts[index])) &&
             (capacity->metrics_shards_per_stage_upper_bound == 1 ||
              bq_retirement_worker_streams_open(campaign->arena, streams, BQ_RETIREMENT_WORKER_STREAM_METRICS, index,
                  bq_retirement_worker_tags[index], 1, capacity->metrics_shards_per_stage_upper_bound - 1u, 0,
                  &campaign->streams_opened, &campaign->metrics[index])) &&
             bq_retirement_worker_streams_open(campaign->arena, streams, BQ_RETIREMENT_WORKER_STREAM_SAMPLES, index, NULL,
                 0, capacity->sample_shards_per_stage, (unsigned)rows, &campaign->streams_opened,
                 &campaign->samples[index]);
    return ok;
}

/* One stage's streams as the driver takes them. */
BUSTER_GLOBAL_LOCAL BqRetirementUnitCampaignStreams bq_retirement_worker_stage_view(
    BqRetirementWorkerCampaign const* campaign, u32 stage)
{
    BqRetirementUnitCampaignStreams view = {campaign->transcripts[stage].streams, campaign->metrics[stage].streams,
        campaign->samples[stage].streams, campaign->transcripts[stage].count, campaign->metrics[stage].count,
        campaign->samples[stage].count};
    return view;
}

/* A finished stage's streams into the result store: its transcript and
 * metrics shards as far as the driver used them, and every sample shard. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_stage_publish(BqRetirementWorkerCampaign* campaign, u32 stage)
{
    unsigned transcripts = campaign->driver.transcript_shards[stage], metrics = campaign->driver.metrics_shards[1 + stage];
    bool ok = transcripts && metrics && transcripts - 1u <= campaign->transcripts[stage].count &&
              metrics - 1u <= campaign->metrics[stage].count &&
              bq_retirement_worker_streams_publish(&campaign->store, &campaign->transcripts_first[stage], 1) &&
              bq_retirement_worker_streams_publish(&campaign->store, &campaign->transcripts[stage], transcripts - 1u) &&
              bq_retirement_worker_streams_publish(&campaign->store, &campaign->metrics_first[stage], 1) &&
              bq_retirement_worker_streams_publish(&campaign->store, &campaign->metrics[stage], metrics - 1u) &&
              bq_retirement_worker_streams_publish(&campaign->store, &campaign->samples[stage],
                                                   campaign->samples[stage].count);
    return ok;
}

/* The sizes the store plan binds for D's documents, retained before any
 * timing as a new read-only file in the campaign directory
 * (BQ_RETIREMENT_WORKER_SIZED_NAME: the header, then one `<path> <bytes>`
 * line per document in bq_retirement_unit_campaign_document_paths order), so
 * the written documents can be checked against them afterwards. */
#define BQ_RETIREMENT_WORKER_SIZED_NAME "documents-sized.txt"
#define BQ_RETIREMENT_WORKER_SIZED_HEADER "BQ-RETIREMENT-UNIT-CAMPAIGN-SIZED-V1\n"
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_sized_write(int directory,
    uint64_t const bytes[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS])
{
    char text[512];
    int length = snprintf(text, sizeof(text), "%s", BQ_RETIREMENT_WORKER_SIZED_HEADER);
    for (u32 index = 0; length > 0 && (size_t)length < sizeof(text) && index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS;
         index += 1)
    {
        int line = snprintf(text + length, sizeof(text) - (size_t)length, "%s %" PRIu64 "\n",
                            bq_retirement_unit_campaign_document_paths[index], bytes[index]);
        length = line > 0 ? length + line : -1;
    }
    bool ok = directory >= 3 && length > 0 && (size_t)length < sizeof(text);
    int file = ok ? openat(directory, BQ_RETIREMENT_WORKER_SIZED_NAME,
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && file >= 0 && bq_write_all(file, (u8 const*)text, (u32)length) && fchmod(file, 0400) == 0 &&
         fsync(file) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    ok = ok && fsync(directory) == 0;
    return ok;
}

/* MEASURING through the store plan: the acknowledgement, the commands from
 * the row plan, both stages bound after it, the store-based bind, the timed
 * rows and documents sized for lane E (and retained), the store planned and
 * the untimed streams published, then attach. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_campaign_bind(BqRetirementWorkerCampaign* campaign,
    BqRetirementCampaignUnitStore const* unit, BqPhaseChannel* phases, int cancellation_fd, u64 deadline_ns,
    BqJob const* job, BqRetirementUnitGate const* unit_gate, char const ready_sha256[SHA256_HEX_CAPACITY],
    int result_root, BqRetirementUnitCampaignDocumentSources const* sources)
{
    BqRetirementCorrectness const* gate = &unit_gate->correctness;
    BqRetirementWorkerLayout const* layout = &campaign->layout;
    BqError result = bq_retirement_unit_campaign_measuring(&campaign->driver) ? BQ_OK : BQ_WORKER_MISMATCH;
    u64 bound_ns = tp_process_monotonic_ns();
    if (result == BQ_OK &&
        !(bq_retirement_campaign_plan_commands(&campaign->row_plan, unit_gate, &campaign->commands) &&
          bq_retirement_worker_layout_build(campaign->arena, gate, &campaign->layout) &&
          campaign->commands.groups == layout->groups && campaign->commands.runtime_count == layout->runtime_count))
        result = BQ_RECIPE_MISMATCH;
    /* The timed object groups' response files, for both sides. */
    for (u32 group = 0; result == BQ_OK && group < gate->batch_group_count; group += 1)
        for (u32 side = 0; result == BQ_OK && side < 2; side += 1)
            if (!bq_retirement_worker_input_list(campaign->directories[BQ_RETIREMENT_WORKER_WORK],
                                                 &gate->batch_groups[group].contract[side]))
                result = BQ_IO;
    if (result == BQ_OK && !bq_retirement_worker_stages_open(campaign, job, bound_ns)) result = BQ_IO;
    size_t count = campaign->commands.count, slots = (size_t)layout->groups + layout->runtime_count;
    TpRetirementMeasuredCommand const* commands[2] = {campaign->commands.commands,
                                                      campaign->commands.commands ? campaign->commands.commands + count : NULL};
    if (result == BQ_OK)
    {
        campaign->snapshots = bq_retirement_worker_allocate(campaign->arena, 4u * slots, sizeof(*campaign->snapshots));
        campaign->identities = bq_retirement_worker_allocate(campaign->arena, slots, sizeof(*campaign->identities));
        campaign->review.group_stages = layout->group_stages;
        campaign->review.group_count = layout->groups;
        if (!campaign->snapshots || !campaign->identities ||
            !bq_retirement_unit_campaign_derive(gate, &campaign->pins, ready_sha256, &campaign->driver.untimed_records,
                &campaign->stages[0].samples, &campaign->stages[1].samples, commands[0], commands[1], count,
                &campaign->plan, campaign->plan_sha256, campaign->context_sha256))
            result = BQ_RECIPE_MISMATCH;
    }
    BqRetirementCampaignRequest request = {gate, &campaign->campaign, &campaign->plan, &campaign->stages[0].samples,
        &campaign->stages[1].samples, commands[0], commands[1], campaign->snapshots, 4u * slots, campaign->identities,
        slots, &campaign->review, campaign->plan_sha256, campaign->context_sha256, &campaign->row_plan};
    if (result == BQ_OK)
        result = bq_retirement_campaign_service_bind_unit_pinned(unit, phases, cancellation_fd, deadline_ns, job->id,
            job->token, &campaign->driver.untimed_records, unit_gate, &request, &campaign->held, &campaign->ready,
            &campaign->binding);
    /* Lane E's layout: the timed rows' dimension values from the pinned rows. */
    unsigned timed = 0;
    if (result == BQ_OK)
    {
        campaign->timed_rows = bq_retirement_worker_allocate(campaign->arena, layout->rows, sizeof(*campaign->timed_rows));
        campaign->compose_rows = bq_retirement_worker_allocate(campaign->arena, layout->rows,
                                                               sizeof(*campaign->compose_rows));
        result = campaign->timed_rows && campaign->compose_rows ?
                 bq_retirement_campaign_service_timed_rows(unit, gate, campaign->timed_rows, layout->rows, &timed) : BQ_IO;
    }
    for (u32 index = 0; result == BQ_OK && index < timed; index += 1)
    {
        TpRetirementTimedRow const* row = campaign->timed_rows + index;
        campaign->compose_rows[index] = (TpRetirementComposeRow){row->id, row->group, row->runtime, {0}};
        for (u32 dimension = 0; dimension < TP_RETIREMENT_COMPOSE_DIMENSIONS; dimension += 1)
            campaign->compose_rows[index].dimensions[dimension] = row->dimensions[dimension];
    }
    campaign->compose_layout = (TpRetirementComposeLayout){campaign->compose_rows, layout->kinds, timed, layout->groups,
                                                           gate->prepared.rows, campaign->untimed_build.groups};
    if (result == BQ_OK && timed != layout->rows) result = BQ_SOURCE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_worker_stage_streams(campaign)) result = BQ_IO;
    /* D's documents are lane E's prior entries: sized before the plan binds
     * their bytes. */
    if (result == BQ_OK &&
        !bq_retirement_unit_campaign_documents_measure(&campaign->driver, &campaign->campaign, sources,
                                                       campaign->document_bytes))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_worker_sized_write(campaign->directory, campaign->document_bytes))
        result = BQ_IO;
    /* The binding context's evidence files are external entries of the plan. */
    if (result == BQ_OK)
        result = bq_retirement_worker_evidence_measure(campaign->arena, unit->installed, unit->profile,
                                                       &campaign->evidence_entries, &campaign->evidence_bytes);
    if (result == BQ_OK)
    {
        campaign->store_files = bq_retirement_worker_allocate(campaign->arena, TP_RETIREMENT_STORE_FILES,
                                                              sizeof(*campaign->store_files));
        if (!campaign->store_files ||
            !tp_retirement_store_open(&campaign->store, result_root, campaign->store_files, TP_RETIREMENT_STORE_FILES))
            result = BQ_WORKSPACE_MISMATCH;
    }
    if (result == BQ_OK &&
        !(bq_retirement_worker_declaration(&campaign->campaign.capacity, campaign->document_bytes,
                                           &campaign->declaration) &&
          bq_retirement_worker_store_plan(&campaign->store, &campaign->campaign.capacity, &campaign->compose_layout,
              campaign->pins.pairs, campaign->code_capacity, &campaign->declaration, campaign->evidence_entries,
              campaign->evidence_bytes, &campaign->store_plan, &campaign->family)))
        result = BQ_RESOURCE_MISMATCH;
    /* The finished untimed streams are the first store files. */
    if (result == BQ_OK &&
        !(bq_retirement_worker_streams_publish(&campaign->store, &campaign->untimed_records, 1) &&
          campaign->driver.metrics_shards[0] <= campaign->untimed_shards.count &&
          bq_retirement_worker_streams_publish(&campaign->store, &campaign->untimed_shards,
                                               campaign->driver.metrics_shards[0])))
        result = BQ_IO;
    if (result == BQ_OK &&
        !bq_retirement_unit_campaign_attach(&campaign->driver, &campaign->binding, &campaign->ready, &campaign->pins,
                                            &campaign->store_plan, &campaign->family))
        result = BQ_RECIPE_MISMATCH;
    return result;
}

/* A written document's size is the one the store plan bound. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_documents_sized(BqRetirementWorkerCampaign const* campaign, u32 first,
    u32 last)
{
    bool ok = true;
    for (u32 index = first; ok && index < last; index += 1)
        ok = campaign->driver.documents[index].bytes == campaign->document_bytes[index];
    return ok;
}

/* The documents, A/A, the admission, the post-A/A document, the freeze, A/B
 * and READY on the staged post-sample record; each finished stage's streams
 * are published. Production has no admission authority: the driver refuses
 * without a failure of its own and the campaign stops there
 * (BQ_RETIREMENT_WORKER_CAMPAIGN_UNAUTHORIZED). */
#define BQ_RETIREMENT_WORKER_CAMPAIGN_UNAUTHORIZED BQ_UNSUPPORTED
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_campaign_stages(BqRetirementWorkerCampaign* campaign,
    BqRetirementUnitCampaignDocumentSources const* sources)
{
    BqRetirementUnitCampaign* driver = &campaign->driver;
    size_t count = campaign->commands.count;
    BqError result = bq_retirement_unit_campaign_documents(driver, sources) ? BQ_OK : BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_worker_documents_sized(campaign, 0, BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA))
        result = BQ_CORRUPT;
    BqRetirementUnitCampaignStreams streams = bq_retirement_worker_stage_view(campaign, 0);
    if (result == BQ_OK && !bq_retirement_unit_campaign_stage(driver, campaign->commands.commands, count, &streams))
        result = BQ_WORKER_FAILED;
    if (result == BQ_OK && !bq_retirement_worker_stage_publish(campaign, 0)) result = BQ_IO;
    BqRetirementUnitCampaignAdmission admission = {campaign->plan_sha256, campaign->context_sha256,
        driver->post_aa_sha256, NULL, NULL, 0, 0};
#if defined(BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA)
    char receipt[BQ_RETIREMENT_UNIT_CAMPAIGN_AA_RECEIPT_BYTES_MAX], receipt_sha256[SHA256_HEX_CAPACITY] = {0};
    u32 receipt_bytes = 0;
    if (result == BQ_OK &&
        bq_retirement_worker_campaign_fixture_receipt(driver, &campaign->campaign, receipt, &receipt_bytes,
                                                      receipt_sha256))
    {
        admission.receipt_sha256 = receipt_sha256;
        admission.receipt = (unsigned char const*)receipt;
        admission.receipt_bytes = receipt_bytes;
        admission.admitted = 1;
    }
#endif
    if (result == BQ_OK && !bq_retirement_unit_campaign_admit(driver, &admission))
        result = driver->failure.reason ? BQ_RECIPE_MISMATCH : BQ_RETIREMENT_WORKER_CAMPAIGN_UNAUTHORIZED;
    /* The admitted receipt's bytes, which the binding names: an admission
     * without them, or with more than the driver's receipt cap, refuses
     * here rather than at composition. */
    if (result == BQ_OK && !(admission.receipt && admission.receipt_bytes &&
                             admission.receipt_bytes <= sizeof(campaign->aa_receipt)))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
    {
        memcpy(campaign->aa_receipt, admission.receipt, admission.receipt_bytes);
        campaign->aa_receipt_bytes = admission.receipt_bytes;
    }
    if (result == BQ_OK &&
        !(bq_retirement_unit_campaign_post_aa_document(driver, sources) &&
          bq_retirement_worker_documents_sized(campaign, BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA,
                                               BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS)))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_unit_campaign_freeze(driver)) result = BQ_RECIPE_MISMATCH;
    streams = bq_retirement_worker_stage_view(campaign, 1);
    if (result == BQ_OK && !bq_retirement_unit_campaign_stage(driver, campaign->commands.commands + count, count, &streams))
        result = BQ_WORKER_FAILED;
    if (result == BQ_OK && !bq_retirement_worker_stage_publish(campaign, 1)) result = BQ_IO;
    if (result == BQ_OK &&
        !bq_retirement_worker_streams_open(campaign->arena, campaign->directories[BQ_RETIREMENT_WORKER_STREAMS],
                                           BQ_RETIREMENT_WORKER_STREAM_RECORD, 0, NULL, 0, 1, 0,
                                           &campaign->streams_opened, &campaign->record))
        result = BQ_IO;
    if (result == BQ_OK && !bq_retirement_unit_campaign_ready(driver, campaign->record.streams[0]))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_worker_streams_publish(&campaign->store, &campaign->record, 1)) result = BQ_IO;
    return result;
}

/* A driver's retained failure as the producer's BqError: cancellation,
 * the absolute deadline, a failed phase exchange, a failed launch or a
 * refused step. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_campaign_error(BqRetirementUnitCampaignFailure const* failure,
    BqError otherwise)
{
    BqError result = otherwise;
    if (failure->reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CANCELLED || failure->cancelled)
        result = BQ_WORKER_CANCEL_SIGNAL;
    else if (failure->reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_DEADLINE) result = BQ_WORKER_TIMEOUT;
    else if (failure->reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CHANNEL) result = BQ_WORKER_MISMATCH;
    else if (failure->reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH) result = BQ_WORKER_FAILED;
    else if (failure->reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED) result = BQ_RECIPE_MISMATCH;
    return result;
}

/* The retained failure: the producer's result and lane D's failure record,
 * one key per line, as a new read-only file in the campaign directory. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_failure_write(int directory, BqError result,
    BqRetirementUnitCampaignFailure const* failure)
{
    char text[BQ_RETIREMENT_WORKER_FAILURE_BYTES];
    int length = snprintf(text, sizeof(text), BQ_RETIREMENT_WORKER_FAILURE_HEADER "result=%d\nreason=%u\nstep=%u\n"
        "stage=%u\nlaunched=%u\nafter=%u\nkind=%u\ngroup=%u\nrow=%u\nvariant=%u\nphase=%u\npurpose=%u\nround=%d\n"
        "pair=%d\nwarmup=%d\nstatus=%d\nexit=%d\nsignal=%d\ntimed-out=%d\ncancelled=%d\nlaunch-error=%d\n"
        "sequence=%" PRIu64 "\nat=%" PRIu64 "\nlog-bytes=%" PRIu64 "\nlog-sha256=%s\nretained-bytes=%" PRIu64
        "\nretained-sha256=%s\n", (int)result, failure->reason, failure->step, failure->stage, failure->launched,
        failure->after, failure->kind, failure->group, failure->row, failure->variant, failure->phase, failure->purpose,
        failure->round, failure->pair, failure->warmup, failure->status, failure->exit_code, failure->signal_number,
        failure->timed_out, failure->cancelled, failure->launch_error, failure->sequence, failure->at_ns,
        failure->log_bytes, failure->log_sha256, failure->retained_bytes, failure->retained_sha256);
    bool ok = directory >= 3 && length > 0 && (size_t)length < sizeof(text);
    int file = ok ? openat(directory, BQ_RETIREMENT_WORKER_FAILURE_NAME,
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && file >= 0 && bq_write_all(file, (u8 const*)text, (u32)length) && fchmod(file, 0400) == 0 &&
         fsync(file) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    ok = ok && fsync(directory) == 0;
    return ok;
}

/* Everything in reverse: the store (aborted; an unfinished campaign never
 * composes), the campaign (poisoned), the staged streams and spools, the
 * plans, the rows, the ready record and held binaries, the directories and
 * the arena. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_campaign_release(BqRetirementWorkerCampaign* campaign)
{
    bool ok = true;
    if (campaign->store.root >= 0)
    {
        if (!campaign->composed) tp_retirement_store_abort(&campaign->store, NULL);
        tp_retirement_store_close(&campaign->store);
    }
    tp_retirement_campaign_poison(&campaign->campaign);
    BqRetirementWorkerStreamGroup* groups[] = {&campaign->record, &campaign->untimed_records, &campaign->untimed_shards,
        &campaign->transcripts_first[0], &campaign->transcripts_first[1], &campaign->transcripts[0],
        &campaign->transcripts[1], &campaign->metrics_first[0], &campaign->metrics_first[1], &campaign->metrics[0],
        &campaign->metrics[1], &campaign->samples[0], &campaign->samples[1]};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(groups); index += 1)
        if (!bq_retirement_worker_streams_close(groups[index])) ok = false;
    for (u32 index = 0; index < TP_RETIREMENT_CAMPAIGN_STAGES; index += 1)
        if (campaign->stages[index].spool && fclose(campaign->stages[index].spool) != 0) ok = false;
    bq_retirement_campaign_plan_commands_release(&campaign->commands);
    bq_retirement_worker_untimed_release(&campaign->untimed_build);
    bq_retirement_documents_partition_release(&campaign->partition);
    bq_retirement_documents_population_release(&campaign->population);
    if (campaign->row_plan.owned && !bq_retirement_row_plan_release(&campaign->row_plan)) ok = false;
    bq_retirement_campaign_ready_release(&campaign->ready);
    if (campaign->held.owned) bq_retirement_binaries_release(&campaign->held);
    for (u32 index = 0; index < BQ_RETIREMENT_WORKER_DIRECTORIES; index += 1)
        if (campaign->directories[index] >= 0 && close(campaign->directories[index]) != 0) ok = false;
    if (campaign->directory >= 0 && close(campaign->directory) != 0) ok = false;
    if (campaign->attempt >= 0 && close(campaign->attempt) != 0) ok = false;
    if (campaign->arena) arena_destroy(campaign->arena, 1);
    return ok;
}

/* After the ready record: SETTLING, MEASURING, the campaign through READY,
 * then composition, the authority, MEASURED and the result's manifest
 * (bq_retirement_worker_compose). Returns BQ_OK only once the manifest is
 * written; any other result retains the failure. Everything is released
 * before it returns. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_campaign_run(BqRetirementCampaignUnitStore const* unit,
    BqPhaseChannel* phases, int cancellation_fd, u64 deadline_ns, int result_root, char const* result_path,
    char const* adapter, BqJob const* job, BqRetirementProjection const* projection,
    BqRetirementUnitGate const* unit_gate, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const ready_sha256[SHA256_HEX_CAPACITY])
{
    BqRetirementWorkerCampaign* campaign = calloc(1, sizeof(*campaign));
    BqError result = campaign && unit && phases && job && projection && unit_gate && result_root >= 0 ? BQ_OK : BQ_IO;
    if (campaign)
    {
        campaign->attempt = campaign->directory = -1;
        for (u32 index = 0; index < BQ_RETIREMENT_WORKER_DIRECTORIES; index += 1) campaign->directories[index] = -1;
        campaign->held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
        campaign->store.root = -1;
        campaign->arena = arena_create((ArenaCreation){.reserved_size = BQ_RETIREMENT_WORKER_ARENA_BYTES,
                                                       .flags = {.no_pool = 1}});
        if (!campaign->arena) result = BQ_IO;
    }
    if (result == BQ_OK) result = bq_retirement_worker_directories_open(campaign, unit->workspaces, job);
    BqRetirementUnitCampaignDocumentSources sources = {result_root, campaign ? &campaign->population : NULL,
                                                       unit ? unit->profile : (String8){0}};
    if (result == BQ_OK)
        result = bq_retirement_worker_campaign_settle(campaign, unit, phases, cancellation_fd, deadline_ns, job,
                                                      projection, unit_gate, preparation_sha256, ready_sha256);
    if (result == BQ_OK) result = bq_retirement_unit_stop_reason(cancellation_fd, deadline_ns, BQ_OK);
    if (result == BQ_OK)
        result = bq_retirement_worker_campaign_bind(campaign, unit, phases, cancellation_fd, deadline_ns, job,
                                                    unit_gate, ready_sha256, result_root, &sources);
    if (result == BQ_OK) result = bq_retirement_worker_campaign_stages(campaign, &sources);
    if (result == BQ_OK)
        result = bq_retirement_worker_compose(campaign, unit, job, unit_gate, result_root, result_path, adapter,
                                              preparation_sha256, ready_sha256, deadline_ns);
    BqRetirementUnitCampaignFailure failure = campaign ? bq_retirement_unit_campaign_failure(&campaign->driver) :
                                                         (BqRetirementUnitCampaignFailure){0};
    if (result != BQ_OK && failure.reason) result = bq_retirement_worker_campaign_error(&failure, result);
    if (result != BQ_OK) result = bq_retirement_unit_stop_reason(cancellation_fd, deadline_ns, result);
    /* A launch killed by its group (cancelled, timed out or refused) can
     * leave orphans reparented to this subreaper; each is killed and reaped
     * here so the failure stands as retained. One found after a READY
     * campaign fails it. */
    bool lingering = false;
    bool swept = bq_retirement_check_sweep(&lingering);
    if (result == BQ_OK && (lingering || !swept)) result = BQ_CLEANUP_FAILED;
    /* A campaign that began keeps its failure (driver or producer). */
    if (result != BQ_OK && campaign && campaign->begun &&
        !bq_retirement_worker_failure_write(campaign->directory, result, &failure))
        result = BQ_IO;
    if (campaign && !bq_retirement_worker_campaign_release(campaign) && result == BQ_OK) result = BQ_IO;
    free(campaign);
    return result;
}
