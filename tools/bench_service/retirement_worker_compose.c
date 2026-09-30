/* Composition, the receipt authority and MEASURED of the retirement producer
 * (#881, PR 3 of 4).
 *
 * Ownership: the worker-unit producer's steps after lane D's READY.
 * retirement_worker_campaign.c calls bq_retirement_worker_compose with the
 * campaign it holds; this file is compiled only into the service translation
 * unit, after retirement_worker_campaign.c and before retirement_worker_unit.c.
 * In order:
 *   1. the timed rows (bq_retirement_campaign_service_timed_rows, bound in
 *      MEASURING) through lane D's handoff (bq_retirement_unit_handoff), whose
 *      retained entries must be the store plan's pre-timing declaration
 *      (bq_retirement_unit_handoff_retained_matches);
 *   2. the A/A admission receipt the campaign admitted, written into the
 *      result root (BQ_RETIREMENT_WORKER_ADMISSION_PATH), and the #511 binding
 *      document the composer reads (bq_retirement_worker_binding_write,
 *      BQ_RETIREMENT_WORKER_BINDING_PATH) from the pinned binding context
 *      (bq_retirement_worker_binding_import) and lane D's documents;
 *   2b. every evidence file the context names, published into the result
 *      root at the size and digest the context binds
 *      (bq_retirement_worker_evidence_publish: the subjects' binaries from
 *      the held descriptors, the rest from the installed evidence directory
 *      BQ_RETIREMENT_WORKER_EVIDENCE_NAME) and handed to the composer with
 *      the admission receipt as the rest of the validator's pre-replay
 *      closure (TpRetirementComposeRequest.closure), which it seals;
 *   3. lane E's composer over the result store (tp_retirement_compose) and the
 *      producer authority it issues into <attempt>/retirement-authority/
 *      (tp_retirement_store_receipt_authority), with the attempt's result
 *      directory as the store root;
 *   4. the context chain beside the authority (retirement_context_chain.h,
 *      mode 0400) and MEASURED carrying the authority digest
 *      (bq_retirement_unit_campaign_measured);
 *   5. after MEASURED is acknowledged (the coordinator has written its
 *      worker-phase-4 receipt), the retirement result's bundle index and
 *      manifest (bq_retirement_worker_result_write).
 * A failure stops before MEASURED; everything the steps published stays as
 * evidence and nothing is replaced.
 *
 * The binding context (BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME, pinned by
 * binding-context-sha256=) is the reviewed, pre-campaign part of the #511
 * record: one canonical-JSON line per section (contract, execution,
 * measurement, population, producer, provenance, requested_work, rules,
 * subjects, support) and the workflow's admission record descriptor. Every
 * field the unit can check is checked against a pin or a plan fact before
 * anything is written (bq_retirement_worker_binding_check); nothing is taken
 * from candidate output. The binding's execution names the admission receipt
 * through a fixed sentinel descriptor in the context, which the writer
 * replaces; its sealed-result and independent-replay phases are the pending
 * sentinels (BQ_RETIREMENT_WORKER_PENDING_SHA256) that lane F replaces.
 *
 * The evidence (bq_retirement_worker_evidence_sites): every {path, bytes,
 * sha256} descriptor the context's sections name, under the validator's
 * _all_artifacts names, except the admission receipt (the producer's own)
 * and the workflow phases (the composer's and lane F's). Each is published
 * under the result-root name lane F's replay maps its binding path to
 * (bq_retirement_worker_evidence_map: BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX
 * and a nested path's segments joined by `--`, injectively; a single-segment
 * path is refused), which cannot name a store file, composer output, control
 * file or phase receipt; the composer seals it under its
 * binding path (TpRetirementComposeRequest.closure_stored). The
 * store plan reserves their exact count and bytes before timing
 * (bq_retirement_worker_evidence_measure); a file whose bytes differ from
 * the context's descriptor is refused, never substituted.
 *
 * Map: BqRetirementWorkerBindingContext, bq_retirement_worker_json_node,
 * bq_retirement_worker_json_text, bq_retirement_worker_json_integer,
 * bq_retirement_worker_binding_parse (the canonical-section check),
 * bq_retirement_worker_binding_import, bq_retirement_worker_binding_check,
 * bq_retirement_worker_admission_write, bq_retirement_worker_binding_render
 * (also the coordinator's re-rendering), bq_retirement_worker_binding_write,
 * BqRetirementWorkerEvidenceSite, bq_retirement_worker_evidence_map, bq_retirement_worker_evidence_list,
 * bq_retirement_worker_evidence_measure, bq_retirement_worker_evidence_open,
 * bq_retirement_worker_evidence_publish, BqRetirementWorkerCompose,
 * bq_retirement_worker_compose_request,
 * bq_retirement_worker_authority_publish, bq_retirement_worker_chain_carried,
 * bq_retirement_worker_bundle_write, bq_retirement_worker_manifest_format
 * (which bq_worker_result_validate's retirement branch re-formats),
 * bq_retirement_worker_result_write, bq_retirement_worker_compose.
 */
#include "retirement_context_chain.h"
#include <dirent.h>

/* The producer's result-root files beside the store and its composer
 * outputs (reserved in the store plan: BQ_RETIREMENT_WORKER_BINDING_ENTRIES). */
#define BQ_RETIREMENT_WORKER_BINDING_PATH "retirement-binding.json"
#define BQ_RETIREMENT_WORKER_ADMISSION_PATH "retirement-aa-admission.json"
#define BQ_RETIREMENT_WORKER_SEALED_PATH "retirement-sealed-result.json"
/* The phases lane F writes after an independent replay. */
#define BQ_RETIREMENT_WORKER_REPLAY_PATH "retirement-independent-replay.json"
#define BQ_RETIREMENT_WORKER_PENDING_SHA256 "0000000000000000000000000000000000000000000000000000000000000000"
/* The attempt's private authority root (retirement_coordinator.c reads it). */
#define BQ_RETIREMENT_UNIT_AUTHORITY_DIRECTORY "retirement-authority"
/* The pinned binding context, installed under recipes/. */
#define BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME "native-retirement-performance-v1.binding-context"
#define BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN "binding-context-sha256="
#define BQ_RETIREMENT_WORKER_BINDING_CONTEXT_HEADER "BQ-RETIREMENT-BINDING-CONTEXT-V1"
/* The installed evidence directory under recipes/: every file the binding
 * context names except the subjects' binaries (held) and the producer's own
 * admission receipt, under its result-root name (bq_retirement_worker_evidence_map).
 * The context's pin covers each file's digest. */
#define BQ_RETIREMENT_WORKER_EVIDENCE_NAME "native-retirement-performance-v1.evidence"
#define BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX "retirement-evidence-"
#define BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP 128u
BUSTER_CT_CHECK(BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP <= BQ_WORKER_BUNDLE_PATH_CAP);
/* The written binding is one bundle file. */
#define BQ_RETIREMENT_WORKER_BINDING_CONTEXT_CAP (BQ_WORKER_BUNDLE_FILE_CAP - 4096u)
#define BQ_RETIREMENT_WORKER_BINDING_SCHEMA "buster-native-retirement-performance-binding-v1"
#define BQ_RETIREMENT_WORKER_BINDING_DECISION "native-retirement-performance-v1"
#define BQ_RETIREMENT_WORKER_WORKFLOW_SCHEMA "buster-native-retirement-performance-workflow-v1"
/* The admission receipt sentinel the context's execution.host carries, and
 * where it sits: canonical JSON sorts keys, so the execution section opens
 * with its host object (host < job_ownership < lease < native_execution <
 * profile < service) and host with its admission receipt
 * (aa_admission_receipt < machine_id < qualification_receipt), the key sets
 * the #511 validator requires exactly. */
#define BQ_RETIREMENT_WORKER_ADMISSION_PREFIX "{\"host\":{"
#define BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL \
    "\"aa_admission_receipt\":{\"bytes\":1,\"path\":\"" BQ_RETIREMENT_WORKER_ADMISSION_PATH "\",\"sha256\":\"" \
    BQ_RETIREMENT_WORKER_PENDING_SHA256 "\"}"
/* The retirement manifest's fixed prefix (bq_worker_result_validate's
 * retirement branch, worker_linux.c). */
#define BQ_RETIREMENT_WORKER_MANIFEST_PREFIX \
    "schema=1\nrecipe=native-retirement-performance-v1\nstatus=succeeded\nstage=retirement\nprocess-result=success\n"

enum
{
    BQ_RETIREMENT_WORKER_BINDING_CONTRACT,
    BQ_RETIREMENT_WORKER_BINDING_EXECUTION,
    BQ_RETIREMENT_WORKER_BINDING_MEASUREMENT,
    BQ_RETIREMENT_WORKER_BINDING_POPULATION,
    BQ_RETIREMENT_WORKER_BINDING_PRODUCER,
    BQ_RETIREMENT_WORKER_BINDING_PROVENANCE,
    BQ_RETIREMENT_WORKER_BINDING_REQUESTED,
    BQ_RETIREMENT_WORKER_BINDING_RULES,
    BQ_RETIREMENT_WORKER_BINDING_SUBJECTS,
    BQ_RETIREMENT_WORKER_BINDING_SUPPORT,
    BQ_RETIREMENT_WORKER_BINDING_ADMISSION,
    BQ_RETIREMENT_WORKER_BINDING_SECTIONS
};
/* The context's line keys, in order; the first ten are also the binding's
 * top-level keys around decision_id, schema and workflow. */
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_worker_binding_sections[BQ_RETIREMENT_WORKER_BINDING_SECTIONS] = {
    "contract", "execution", "measurement", "population", "producer", "provenance", "requested_work", "rules",
    "subjects", "support", "admission"};
/* The support files the profile pins, by #508 role. */
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_worker_binding_roles[6] = {"support_declaration", "manifest",
    "inputs", "rows", "performance_rows", "validator_report"};
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_worker_binding_role_pins[6] = {"support-declaration-sha256=",
    "census-manifest-sha256=", "census-inputs-sha256=", "census-rows-sha256=", "performance-rows-sha256=",
    "validator-report-sha256="};

/* ------------------------------------------------------------ JSON queries */

/* The node at `path` (depth members) below `node`, or TP_COMPOSE_JSON_NONE. */
BUSTER_GLOBAL_LOCAL unsigned bq_retirement_worker_json_node(TpComposeJson const* json, unsigned node,
    char const* const* path, u32 depth)
{
    for (u32 index = 0; node != TP_COMPOSE_JSON_NONE && index < depth; index += 1)
        node = json->nodes[node].kind == TP_COMPOSE_JSON_OBJECT ?
               tp_retirement_compose_json_member(json, node, path[index]) : TP_COMPOSE_JSON_NONE;
    return node;
}

/* Whether the string at `path` is exactly `expected`. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_json_text(TpComposeJson const* json, unsigned node,
    char const* const* path, u32 depth, char const* expected)
{
    unsigned found = bq_retirement_worker_json_node(json, node, path, depth);
    size_t length = expected ? strlen(expected) : 0;
    bool ok = found != TP_COMPOSE_JSON_NONE && json->nodes[found].kind == TP_COMPOSE_JSON_STRING && length &&
              json->nodes[found].length == length && !memcmp(json->nodes[found].text, expected, length);
    return ok;
}

/* Whether the integer at `path` is exactly `expected`. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_json_integer(TpComposeJson const* json, unsigned node,
    char const* const* path, u32 depth, u64 expected)
{
    unsigned found = bq_retirement_worker_json_node(json, node, path, depth);
    char text[24];
    int length = snprintf(text, sizeof(text), "%" PRIu64, expected);
    bool ok = found != TP_COMPOSE_JSON_NONE && json->nodes[found].kind == TP_COMPOSE_JSON_INTEGER && length > 0 &&
              json->nodes[found].length == (u32)length && !memcmp(json->nodes[found].text, text, (size_t)length);
    return ok;
}

/* ---------------------------------------------------------- binding context */

typedef struct BqRetirementWorkerBindingContext
{
    u8* bytes;
    u32 length;
    String8 sections[BQ_RETIREMENT_WORKER_BINDING_SECTIONS];
    TpComposeJson json[BQ_RETIREMENT_WORKER_BINDING_SECTIONS];
    char sha256[SHA256_HEX_CAPACITY];
} BqRetirementWorkerBindingContext;

BUSTER_GLOBAL_LOCAL void bq_retirement_worker_binding_release(BqRetirementWorkerBindingContext* context)
{
    if (context)
    {
        free(context->bytes);
        *context = (BqRetirementWorkerBindingContext){0};
    }
}

/* The context's bytes: the header, then exactly one `<key>=<canonical JSON>`
 * line per section in bq_retirement_worker_binding_sections order. Each value
 * must be its own canonical encoding (tp_retirement_compose_json_canonical)
 * and the admission record a {bytes, path, sha256} descriptor. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_binding_parse(Arena* arena, BqRetirementWorkerBindingContext* context)
{
    String8 bytes = {(char8*)context->bytes, context->length};
    u64 offset = 0;
    String8 line = {0};
    bool ok = arena && context->bytes && !memchr(context->bytes, 0, context->length) &&
              bq_next_line(bytes, &offset, &line) && string_equal(line, S8(BQ_RETIREMENT_WORKER_BINDING_CONTEXT_HEADER));
    for (u32 index = 0; ok && index < BQ_RETIREMENT_WORKER_BINDING_SECTIONS; index += 1)
    {
        char const* key = bq_retirement_worker_binding_sections[index];
        size_t key_length = strlen(key);
        char* canonical = NULL;
        size_t canonical_length = 0;
        ok = bq_next_line(bytes, &offset, &line) && line.length > key_length + 1u &&
             !memcmp(line.pointer, key, key_length) && line.pointer[key_length] == '=';
        String8 value = ok ? (String8){line.pointer + key_length + 1u, line.length - key_length - 1u} : (String8){0};
        ok = ok && tp_retirement_compose_json_parse((unsigned char const*)value.pointer, (size_t)value.length, arena,
                                                    &context->json[index]) &&
             context->json[index].nodes[0].kind == TP_COMPOSE_JSON_OBJECT &&
             tp_retirement_compose_json_canonical((unsigned char const*)value.pointer, (size_t)value.length, arena,
                                                  &canonical, &canonical_length) &&
             canonical_length == value.length && !memcmp(canonical, value.pointer, canonical_length);
        if (ok) context->sections[index] = value;
    }
    static char const* const descriptor[] = {"bytes", "path", "sha256"};
    static char const* const sha[] = {"sha256"};
    TpComposeJson const* admission = &context->json[BQ_RETIREMENT_WORKER_BINDING_ADMISSION];
    unsigned digest = ok ? bq_retirement_worker_json_node(admission, 0, sha, 1) : TP_COMPOSE_JSON_NONE;
    ok = ok && offset == bytes.length && tp_retirement_compose_json_keys(admission, 0, descriptor, 3) &&
         digest != TP_COMPOSE_JSON_NONE && admission->nodes[digest].kind == TP_COMPOSE_JSON_STRING &&
         admission->nodes[digest].length == 64;
    return ok;
}

/* The pinned binding context for this profile: missing pin
 * (BQ_RECIPE_MISMATCH), an unreadable or unowned file
 * (BQ_CONFIGURATION_MISMATCH), or bytes that are not the pinned ones or not
 * a context (BQ_RECIPE_MISMATCH). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_binding_import(Arena* arena, int installed, String8 profile,
    BqRetirementWorkerBindingContext* context)
{
    char pin[SHA256_HEX_CAPACITY] = {0};
    if (context) *context = (BqRetirementWorkerBindingContext){0};
    BqError result = arena && installed >= 0 && context &&
                     bq_retirement_profile_sha(profile, S8(BQ_RETIREMENT_WORKER_BINDING_CONTEXT_PIN), pin) ?
                     BQ_OK : BQ_RECIPE_MISMATCH;
    int recipes = result == BQ_OK ? openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (result == BQ_OK)
        result = recipes >= 0 && bq_owned_directory(recipes, false, true) &&
                 bq_retirement_reference_read_installed(recipes, BQ_RETIREMENT_WORKER_BINDING_CONTEXT_NAME,
                     (u32)BQ_RETIREMENT_WORKER_BINDING_CONTEXT_CAP, &context->bytes, &context->length,
                     context->sha256, NULL) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK && memcmp(context->sha256, pin, SHA256_HEX_CAPACITY)) result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_worker_binding_parse(arena, context)) result = BQ_RECIPE_MISMATCH;
    if (recipes >= 0 && close(recipes) != 0 && result == BQ_OK) result = BQ_CONFIGURATION_MISMATCH;
    if (result != BQ_OK) bq_retirement_worker_binding_release(context);
    return result;
}

/* Every context field the unit holds a fact for: the contract source and
 * the six pinned #508 support files against the profile's pins; the
 * population's rows digest and count against the pinned rows and the gate,
 * and its statistical family against the one lane D derived from them; both
 * subjects' binaries against the held pair the gate sealed; the sampling
 * rules against the frozen campaign values and the #619 plan; and the
 * admission receipt sentinel in execution.host, exactly once. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_binding_check(BqRetirementWorkerBindingContext const* context,
    String8 profile, BqRetirementCorrectness const* gate, char const family_sha256[SHA256_HEX_CAPACITY],
    BqRetirementUnitCampaignPins const* pins, TpRetirementPlan const* plan)
{
    char pin[SHA256_HEX_CAPACITY] = {0};
    TpComposeJson const* json = context->json;
    static char const* const contract[] = {"source", "sha256"};
    bool ok = gate && family_sha256 && pins && plan && bq_retirement_profile_sha(profile, S8("contract-sha256="), pin) &&
              bq_retirement_worker_json_text(&json[BQ_RETIREMENT_WORKER_BINDING_CONTRACT], 0, contract, 2, pin);
    /* support.files: each pinned role exactly once, at its pinned digest. */
    static char const* const files_path[] = {"files"};
    TpComposeJson const* support = &json[BQ_RETIREMENT_WORKER_BINDING_SUPPORT];
    unsigned files = ok ? bq_retirement_worker_json_node(support, 0, files_path, 1) : TP_COMPOSE_JSON_NONE;
    ok = files != TP_COMPOSE_JSON_NONE && support->nodes[files].kind == TP_COMPOSE_JSON_ARRAY;
    u32 seen[6] = {0};
    static char const* const name_path[] = {"name"};
    static char const* const sha_path[] = {"sha256"};
    for (unsigned child = ok ? support->nodes[files].first : TP_COMPOSE_JSON_NONE;
         ok && child != TP_COMPOSE_JSON_NONE; child = support->nodes[child].next)
    {
        for (u32 role = 0; ok && role < 6; role += 1)
        {
            if (!bq_retirement_worker_json_text(support, child, name_path, 1, bq_retirement_worker_binding_roles[role]))
                continue;
            seen[role] += 1;
            ok = bq_retirement_profile_sha(profile, string_from_pointer(bq_retirement_worker_binding_role_pins[role]),
                                           pin) &&
                 bq_retirement_worker_json_text(support, child, sha_path, 1, pin);
        }
    }
    for (u32 role = 0; ok && role < 6; role += 1) ok = seen[role] == 1;
    /* population */
    TpComposeJson const* population = &json[BQ_RETIREMENT_WORKER_BINDING_POPULATION];
    static char const* const rows_sha[] = {"required_rows_sha256"};
    static char const* const rows_count[] = {"required_row_count"};
    static char const* const family[] = {"statistical_family", "sha256"};
    ok = ok && bq_retirement_profile_sha(profile, S8("performance-rows-sha256="), pin) &&
         bq_retirement_worker_json_text(population, 0, rows_sha, 1, pin) &&
         bq_retirement_worker_json_integer(population, 0, rows_count, 1, gate->prepared.rows) &&
         bq_retirement_worker_json_text(population, 0, family, 2, family_sha256);
    /* subjects */
    static char const* const baseline[] = {"baseline", "binary", "sha256"};
    static char const* const candidate[] = {"candidate", "binary", "sha256"};
    TpComposeJson const* subjects = &json[BQ_RETIREMENT_WORKER_BINDING_SUBJECTS];
    ok = ok && bq_retirement_worker_json_text(subjects, 0, baseline, 3, gate->prepared.binary_sha256[0]) &&
         bq_retirement_worker_json_text(subjects, 0, candidate, 3, gate->prepared.binary_sha256[1]);
    /* rules.sampling */
    static char const* const sampling_keys[] = {"seed", "pairs_per_round", "resamples", "bootstrap_members_per_scope",
                                                "cell_members_per_scope", "rounds", "warmups_per_variant"};
    u64 const sampling_values[] = {pins->seed, pins->pairs, pins->resamples, plan->bootstrap_members_per_scope,
                                   plan->cell_members_per_scope, TP_RETIREMENT_ROUNDS, TP_RETIREMENT_WARMUPS};
    TpComposeJson const* rules = &json[BQ_RETIREMENT_WORKER_BINDING_RULES];
    static char const* const sampling_path[] = {"sampling"};
    unsigned sampling = ok ? bq_retirement_worker_json_node(rules, 0, sampling_path, 1) : TP_COMPOSE_JSON_NONE;
    ok = ok && sampling != TP_COMPOSE_JSON_NONE && pins->pairs == plan->pairs_per_round &&
         pins->resamples == plan->resamples && pins->seed == plan->seed;
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(sampling_keys); index += 1)
        ok = bq_retirement_worker_json_integer(rules, sampling, sampling_keys + index, 1, sampling_values[index]);
    /* The admission receipt sentinel, exactly once in execution. */
    static char const* const receipt_path[] = {"host", "aa_admission_receipt", "path"};
    static char const* const receipt_sha[] = {"host", "aa_admission_receipt", "sha256"};
    static char const* const receipt_bytes[] = {"host", "aa_admission_receipt", "bytes"};
    TpComposeJson const* execution = &json[BQ_RETIREMENT_WORKER_BINDING_EXECUTION];
    String8 section = context->sections[BQ_RETIREMENT_WORKER_BINDING_EXECUTION];
    size_t sentinel = strlen(BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL);
    size_t prefix = strlen(BQ_RETIREMENT_WORKER_ADMISSION_PREFIX);
    u32 occurrences = 0;
    for (u64 at = 0; ok && at + sentinel <= section.length; at += 1)
        occurrences += !memcmp(section.pointer + at, BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL, sentinel);
    /* Exactly once, and as execution.host.aa_admission_receipt itself. */
    ok = ok && occurrences == 1 && section.length > prefix + sentinel &&
         !memcmp(section.pointer, BQ_RETIREMENT_WORKER_ADMISSION_PREFIX, prefix) &&
         !memcmp(section.pointer + prefix, BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL, sentinel) &&
         bq_retirement_worker_json_text(execution, 0, receipt_path, 3, BQ_RETIREMENT_WORKER_ADMISSION_PATH) &&
         bq_retirement_worker_json_text(execution, 0, receipt_sha, 3, BQ_RETIREMENT_WORKER_PENDING_SHA256) &&
         bq_retirement_worker_json_integer(execution, 0, receipt_bytes, 3, 1);
    return ok;
}

/* ------------------------------------------------------------- the writers */

/* A new read-only file `name` in `directory` holding `length` bytes; its
 * digest when `digest` is given. Never replaces an entry; a file it created
 * (O_EXCL: its own) and could not complete is removed again. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_file_write(int directory, char const* name, char const* bytes,
    u64 length, char digest[SHA256_HEX_CAPACITY])
{
    int file = directory >= 0 && length && length <= UINT32_MAX ?
               openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    bool created = file >= 0;
    bool ok = created && bq_write_all(file, (u8 const*)bytes, (u32)length) && fchmod(file, 0400) == 0 &&
              fsync(file) == 0;
    if (created && close(file) != 0) ok = false;
    ok = ok && fsync(directory) == 0;
    if (created && !ok) unlinkat(directory, name, 0);
    if (ok && digest) bq_digest(bytes, (u32)length, (char8*)digest);
    return ok;
}

/* The A/A admission receipt the admission step verified, as the binding's
 * execution.host.aa_admission_receipt: its digest must be the one the
 * post-A/A document binds. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_admission_write(int result_root, char const* receipt, u32 length,
    char const admitted_sha256[SHA256_HEX_CAPACITY], char digest[SHA256_HEX_CAPACITY])
{
    char actual[SHA256_HEX_CAPACITY] = {0};
    bool ok = receipt && length && admitted_sha256 && tp_retirement_digest(admitted_sha256);
    if (ok) bq_digest(receipt, length, (char8*)actual);
    ok = ok && !strcmp(actual, admitted_sha256) &&
         bq_retirement_worker_file_write(result_root, BQ_RETIREMENT_WORKER_ADMISSION_PATH, receipt, length, digest);
    return ok;
}

/* One descriptor object of a workflow artifact. */
BUSTER_GLOBAL_LOCAL int bq_retirement_worker_descriptor(char* output, size_t capacity, char const* path, u64 bytes,
    char const* sha256)
{
    int length = snprintf(output, capacity, "{\"bytes\":%" PRIu64 ",\"path\":\"%s\",\"sha256\":\"%s\"}", bytes, path,
                          sha256);
    int result = length > 0 && (size_t)length < capacity ? length : -1;
    return result;
}

/* One contiguous piece of the written binding. */
typedef struct BqRetirementWorkerPiece
{
    char const* pointer;
    u64 length;
} BqRetirementWorkerPiece;

/* The #511 binding the composer reads, canonical JSON (every section is
 * canonical and the keys are emitted in sorted order): the context's
 * sections, the decision and schema, the execution with the admission
 * receipt's descriptor in place of the sentinel, and the workflow: lane D's
 * pre-sample plan and post-A/A binding, the pending sealed-result and
 * independent-replay phases, the context's admission record and D's oracle
 * and result-input plan. The producer writes these bytes
 * (bq_retirement_worker_binding_write); the coordinator renders them again
 * from the documents it derives and requires the same bytes
 * (bq_retirement_coordinator_derive). *bytes receives arena memory. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_binding_render(BqRetirementWorkerBindingContext const* context,
    BqRetirementUnitCampaignDocument const documents[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS], u32 admission_bytes,
    char const admission_sha256[SHA256_HEX_CAPACITY], Arena* arena, char** bytes, u64* length)
{
    char receipt[256], workflow[2048], phases[4][256], records[2][256];
    int receipt_length = snprintf(receipt, sizeof(receipt), "\"aa_admission_receipt\":");
    int descriptor = receipt_length > 0 && admission_sha256 ?
        bq_retirement_worker_descriptor(receipt + receipt_length, sizeof(receipt) - (size_t)receipt_length,
                                        BQ_RETIREMENT_WORKER_ADMISSION_PATH, admission_bytes, admission_sha256) : -1;
    bool ok = context && documents && arena && bytes && length && descriptor > 0 &&
              bq_retirement_worker_descriptor(phases[0], sizeof(phases[0]), BQ_RETIREMENT_WORKER_REPLAY_PATH, 1,
                                              BQ_RETIREMENT_WORKER_PENDING_SHA256) > 0 &&
              bq_retirement_worker_descriptor(phases[1], sizeof(phases[1]),
                  bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA],
                  documents[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA].bytes,
                  documents[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA].sha256) > 0 &&
              bq_retirement_worker_descriptor(phases[2], sizeof(phases[2]),
                  bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE],
                  documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].bytes,
                  documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].sha256) > 0 &&
              bq_retirement_worker_descriptor(phases[3], sizeof(phases[3]), BQ_RETIREMENT_WORKER_SEALED_PATH, 1,
                                              BQ_RETIREMENT_WORKER_PENDING_SHA256) > 0 &&
              bq_retirement_worker_descriptor(records[0], sizeof(records[0]),
                  bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE],
                  documents[BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE].bytes,
                  documents[BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE].sha256) > 0 &&
              bq_retirement_worker_descriptor(records[1], sizeof(records[1]),
                  bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN],
                  documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].bytes,
                  documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].sha256) > 0;
    String8 admission = ok ? context->sections[BQ_RETIREMENT_WORKER_BINDING_ADMISSION] : (String8){0};
    int workflow_length = ok ? snprintf(workflow, sizeof(workflow), ",\"workflow\":{\"phases\":{\"independent_replay\":%s,"
        "\"post_aa_binding\":%s,\"pre_sample_plan\":%s,\"sealed_result\":%s},\"records\":{\"admission\":%.*s,"
        "\"oracle\":%s,\"result_input_plan\":%s},\"schema\":\"" BQ_RETIREMENT_WORKER_WORKFLOW_SCHEMA "\","
        "\"version\":1}}", phases[0], phases[1], phases[2], phases[3], (int)admission.length,
        (char const*)admission.pointer, records[0], records[1]) : -1;
    ok = ok && workflow_length > 0 && (size_t)workflow_length < sizeof(workflow);
    /* The execution section around its sentinel, which the context check
     * placed as execution.host's first member. */
    String8 execution = ok ? context->sections[BQ_RETIREMENT_WORKER_BINDING_EXECUTION] : (String8){0};
    size_t sentinel = strlen(BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL);
    u64 at = strlen(BQ_RETIREMENT_WORKER_ADMISSION_PREFIX);
    ok = ok && at + sentinel < execution.length &&
         !memcmp(execution.pointer, BQ_RETIREMENT_WORKER_ADMISSION_PREFIX, at) &&
         !memcmp(execution.pointer + at, BQ_RETIREMENT_WORKER_ADMISSION_SENTINEL, sentinel);
    /* The pieces in canonical key order. */
    static char const* const middle[] = {"measurement", "population", "producer", "provenance", "requested_work",
                                         "rules"};
    char keys[BUSTER_ARRAY_LENGTH(middle)][32];
    /* Contract (2), decision and execution around the receipt (4), the
     * middle sections (2 each), schema and subjects (2), support (2) and
     * the workflow (1). */
    BqRetirementWorkerPiece pieces[2u + 4u + 2u * BUSTER_ARRAY_LENGTH(middle) + 2u + 2u + 1u];
    u32 count = 0;
    String8 const* sections = context ? context->sections : NULL;
    if (ok)
    {
        static char const decision[] = ",\"decision_id\":\"" BQ_RETIREMENT_WORKER_BINDING_DECISION "\",\"execution\":";
        static char const schema[] = ",\"schema\":\"" BQ_RETIREMENT_WORKER_BINDING_SCHEMA "\",\"subjects\":";
        pieces[count++] = (BqRetirementWorkerPiece){"{\"contract\":", 12};
        pieces[count++] = (BqRetirementWorkerPiece){(char const*)sections[BQ_RETIREMENT_WORKER_BINDING_CONTRACT].pointer,
                                                    sections[BQ_RETIREMENT_WORKER_BINDING_CONTRACT].length};
        pieces[count++] = (BqRetirementWorkerPiece){decision, sizeof(decision) - 1u};
        pieces[count++] = (BqRetirementWorkerPiece){(char const*)execution.pointer, at};
        pieces[count++] = (BqRetirementWorkerPiece){receipt, (u64)(receipt_length + descriptor)};
        pieces[count++] = (BqRetirementWorkerPiece){(char const*)execution.pointer + at + sentinel,
                                                    execution.length - at - sentinel};
        for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(middle); index += 1)
        {
            u32 section = BQ_RETIREMENT_WORKER_BINDING_MEASUREMENT + index;
            int named = snprintf(keys[index], sizeof(keys[index]), ",\"%s\":", middle[index]);
            ok = named > 0 && (size_t)named < sizeof(keys[index]);
            pieces[count++] = (BqRetirementWorkerPiece){keys[index], ok ? (u64)named : 0};
            pieces[count++] = (BqRetirementWorkerPiece){(char const*)sections[section].pointer, sections[section].length};
        }
        pieces[count++] = (BqRetirementWorkerPiece){schema, sizeof(schema) - 1u};
        pieces[count++] = (BqRetirementWorkerPiece){(char const*)sections[BQ_RETIREMENT_WORKER_BINDING_SUBJECTS].pointer,
                                                    sections[BQ_RETIREMENT_WORKER_BINDING_SUBJECTS].length};
        pieces[count++] = (BqRetirementWorkerPiece){",\"support\":", 11};
        pieces[count++] = (BqRetirementWorkerPiece){(char const*)sections[BQ_RETIREMENT_WORKER_BINDING_SUPPORT].pointer,
                                                    sections[BQ_RETIREMENT_WORKER_BINDING_SUPPORT].length};
        pieces[count++] = (BqRetirementWorkerPiece){workflow, (u64)workflow_length};
        ok = ok && count == BUSTER_ARRAY_LENGTH(pieces);
    }
    u64 total = 0;
    for (u32 index = 0; ok && index < count; index += 1) total += pieces[index].length;
    ok = ok && total && total <= BQ_WORKER_BUNDLE_FILE_CAP;
    char* rendered = ok ? bq_retirement_worker_allocate(arena, total, 1) : NULL;
    ok = ok && rendered;
    u64 used = 0;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        memcpy(rendered + used, pieces[index].pointer, (size_t)pieces[index].length);
        used += pieces[index].length;
    }
    if (bytes) *bytes = ok ? rendered : NULL;
    if (length) *length = ok ? total : 0;
    return ok;
}

/* The rendered binding as a new, read-only, synced result-root file; *bytes
 * and digest receive its size and SHA-256. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_binding_write(int result_root, BqRetirementWorkerBindingContext const* context,
    BqRetirementUnitCampaignDocument const documents[BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS], u32 admission_bytes,
    char const admission_sha256[SHA256_HEX_CAPACITY], Arena* arena, u64* bytes, char digest[SHA256_HEX_CAPACITY])
{
    char* rendered = NULL;
    u64 length = 0;
    bool ok = bq_retirement_worker_binding_render(context, documents, admission_bytes, admission_sha256, arena,
                                                  &rendered, &length) &&
              bq_retirement_worker_file_write(result_root, BQ_RETIREMENT_WORKER_BINDING_PATH, rendered, length, digest);
    if (bytes) *bytes = ok ? length : 0;
    return ok;
}

/* ------------------------------------------------------------ the evidence */

/* Where each evidence descriptor sits in the context: the section, the
 * member path, how an array holds it (none, the array's items themselves as
 * in support.files, or each item's `artifact` as in requested_work.items)
 * and, for the subjects' binaries, which held binary supplies the bytes
 * (1 baseline, 2 candidate; 0 the installed evidence directory). The
 * admission receipt is the producer's own
 * (bq_retirement_worker_admission_write) and the workflow phases are the
 * composer's and lane F's. */
enum
{
    BQ_RETIREMENT_WORKER_EVIDENCE_SINGLE,
    BQ_RETIREMENT_WORKER_EVIDENCE_ITEMS,
    BQ_RETIREMENT_WORKER_EVIDENCE_ARTIFACTS
};
typedef struct BqRetirementWorkerEvidenceSite
{
    u32 section, depth, array, held;
    char const* path[2];
    /* The validator's _all_artifacts name; an array item's is
     * `<name>[<index>]<suffix>`. */
    char const* name;
    char const* suffix;
} BqRetirementWorkerEvidenceSite;
BUSTER_GLOBAL_LOCAL BqRetirementWorkerEvidenceSite const bq_retirement_worker_evidence_sites[] = {
    {BQ_RETIREMENT_WORKER_BINDING_CONTRACT, 1, 0, 0, {"source", NULL}, "contract.source", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_SUPPORT, 1, BQ_RETIREMENT_WORKER_EVIDENCE_ITEMS, 0, {"files", NULL},
     "support.files", ""},
    {BQ_RETIREMENT_WORKER_BINDING_SUPPORT, 2, 0, 0, {"validator", "source"}, "support.validator.source", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_REQUESTED, 1, BQ_RETIREMENT_WORKER_EVIDENCE_ARTIFACTS, 0, {"items", NULL},
     "requested_work.items", ".artifact"},
    {BQ_RETIREMENT_WORKER_BINDING_SUBJECTS, 2, 0, 0, {"baseline", "source_snapshot"},
     "subjects.baseline.source_snapshot", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_SUBJECTS, 2, 0, 1, {"baseline", "binary"}, "subjects.baseline.binary", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_SUBJECTS, 2, 0, 0, {"baseline", "build_receipt"},
     "subjects.baseline.build_receipt", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_SUBJECTS, 2, 0, 0, {"candidate", "source_snapshot"},
     "subjects.candidate.source_snapshot", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_SUBJECTS, 2, 0, 2, {"candidate", "binary"}, "subjects.candidate.binary", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_SUBJECTS, 2, 0, 0, {"candidate", "build_receipt"},
     "subjects.candidate.build_receipt", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PRODUCER, 2, 0, 0, {"toolchain", "compiler_binary"},
     "producer.toolchain.compiler_binary", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PRODUCER, 2, 0, 0, {"toolchain", "resource_directory"},
     "producer.toolchain.resource_directory", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PRODUCER, 2, 0, 0, {"build", "configuration"}, "producer.build.configuration", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PRODUCER, 2, 0, 0, {"build", "flags"}, "producer.build.flags", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_MEASUREMENT, 1, 0, 0, {"harness_binary", NULL}, "measurement.harness_binary", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_MEASUREMENT, 1, 0, 0, {"statistics_implementation", NULL},
     "measurement.statistics_implementation", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_EXECUTION, 2, 0, 0, {"service", "recipe"}, "execution.service.recipe", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_EXECUTION, 2, 0, 0, {"profile", "descriptor"}, "execution.profile.descriptor", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_EXECUTION, 2, 0, 0, {"host", "qualification_receipt"},
     "execution.host.qualification_receipt", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_EXECUTION, 2, 0, 0, {"lease", "receipt"}, "execution.lease.receipt", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PROVENANCE, 1, 0, 0, {"relation_receipt", NULL}, "provenance.relation_receipt", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PROVENANCE, 1, 0, 0, {"replay_receipt", NULL}, "provenance.replay_receipt", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PROVENANCE, 1, 0, 0, {"replay_bundle", NULL}, "provenance.replay_bundle", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PROVENANCE, 1, 0, 0, {"census_receipt", NULL}, "provenance.census_receipt", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_PROVENANCE, 1, 0, 0, {"strict_receipt", NULL}, "provenance.strict_receipt", NULL},
    {BQ_RETIREMENT_WORKER_BINDING_ADMISSION, 0, 0, 0, {NULL, NULL}, "workflow.records.admission", NULL},
};

/* One evidence file: its validator name, binding path, result-root name,
 * size, SHA-256 and source. */
typedef struct BqRetirementWorkerEvidence
{
    char name[TP_RETIREMENT_COMPOSE_NAME_BYTES];
    /* The binding path, and the result-root name it is published under. */
    char path[BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP + 1];
    char stored[BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP + 1];
    char sha256[SHA256_HEX_CAPACITY];
    u64 bytes;
    u32 held;
} BqRetirementWorkerEvidence;

typedef struct BqRetirementWorkerEvidenceList
{
    BqRetirementWorkerEvidence items[BQ_RETIREMENT_WORKER_EVIDENCE_CAP];
    u64 bytes;
    u32 count;
} BqRetirementWorkerEvidenceList;

/* The result-root name of a binding path (lane F's replay layout, #1995's
 * evidence_name in tools/bench_service/retirement_lane_f.py, the same rule;
 * bq_prep_worker_unit_evidence_names holds the two to one table):
 * BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX, then the path's segments joined by
 * `--`. Refused, so every `--` in a name is a separator (the mapping is
 * injective) and nothing else in the result root is named: a single-segment
 * path (lane F reads one where it is, never under a flat name), an empty,
 * `.` or `..` segment, a segment containing `--` or beginning or ending
 * with `-`, a byte outside [A-Za-z0-9._-] and `/`, and a mapped name longer
 * than BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_evidence_map(char const* path, u32 length,
    char stored[BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP + 1])
{
    size_t used = strlen(BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX);
    bool ok = path && length && length <= BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP &&
              memchr(path, '/', length) != NULL;
    if (ok) memcpy(stored, BQ_RETIREMENT_WORKER_EVIDENCE_PREFIX, used);
    u32 start = 0;
    for (u32 index = 0; ok && index <= length; index += 1)
    {
        char c = index < length ? path[index] : '/';
        if (c == '/')
        {
            u32 segment = index - start;
            ok = segment && path[start] != '-' && path[index - 1u] != '-' &&
                 !(segment == 1 && path[start] == '.') &&
                 !(segment == 2 && path[start] == '.' && path[start + 1] == '.');
            start = index + 1u;
        }
        else
            ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                 c == '_' || (c == '-' && !(index > start && path[index - 1u] == '-'));
        u32 width = c == '/' ? 2u : 1u;
        ok = ok && (index == length || used + width <= BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP);
        if (ok && index < length)
        {
            memcpy(stored + used, c == '/' ? "--" : &path[index], width);
            used += width;
        }
    }
    if (ok) stored[used] = 0;
    return ok;
}

/* One {path, bytes, sha256} descriptor (other members, such as a support
 * file's name, are the validator's): a binding path with a result-root name
 * (bq_retirement_worker_evidence_map); 1 to BQ_WORKER_BUNDLE_FILE_CAP bytes,
 * without a leading zero; a lowercase SHA-256. Appended unless its path or
 * its result-root name is already listed, which refuses. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_evidence_add(TpComposeJson const* json, unsigned node,
    BqRetirementWorkerEvidenceSite const* site, u32 index_in_array, BqRetirementWorkerEvidenceList* list)
{
    static char const* const path_key[] = {"path"};
    static char const* const bytes_key[] = {"bytes"};
    static char const* const sha_key[] = {"sha256"};
    unsigned path = bq_retirement_worker_json_node(json, node, path_key, 1);
    unsigned bytes = bq_retirement_worker_json_node(json, node, bytes_key, 1);
    unsigned sha = bq_retirement_worker_json_node(json, node, sha_key, 1);
    bool ok = list->count < BQ_RETIREMENT_WORKER_EVIDENCE_CAP && path != TP_COMPOSE_JSON_NONE &&
              bytes != TP_COMPOSE_JSON_NONE && sha != TP_COMPOSE_JSON_NONE &&
              json->nodes[path].kind == TP_COMPOSE_JSON_STRING &&
              json->nodes[bytes].kind == TP_COMPOSE_JSON_INTEGER && json->nodes[bytes].length <= 19u &&
              json->nodes[sha].kind == TP_COMPOSE_JSON_STRING && json->nodes[sha].length == 64u;
    BqRetirementWorkerEvidence* item = ok ? &list->items[list->count] : NULL;
    char stored[BQ_RETIREMENT_WORKER_EVIDENCE_PATH_CAP + 1] = {0};
    ok = ok && bq_retirement_worker_evidence_map(json->nodes[path].text, json->nodes[path].length, stored);
    u64 size = 0;
    ok = ok && (json->nodes[bytes].length == 1u || json->nodes[bytes].text[0] != '0');
    for (u32 index = 0; ok && index < json->nodes[bytes].length; index += 1)
    {
        char c = json->nodes[bytes].text[index];
        ok = c >= '0' && c <= '9';
        size = size * 10u + (u64)(c - '0');
    }
    ok = ok && size && size <= BQ_WORKER_BUNDLE_FILE_CAP;
    if (ok)
    {
        *item = (BqRetirementWorkerEvidence){.bytes = size, .held = site->held};
        memcpy(item->path, json->nodes[path].text, json->nodes[path].length);
        memcpy(item->stored, stored, sizeof(stored));
        memcpy(item->sha256, json->nodes[sha].text, 64u);
        int named = site->array ? snprintf(item->name, sizeof(item->name), "%s[%u]%s", site->name, index_in_array,
                                           site->suffix) :
                                  snprintf(item->name, sizeof(item->name), "%s", site->name);
        ok = named > 0 && (size_t)named < sizeof(item->name) && tp_retirement_digest(item->sha256);
    }
    for (u32 index = 0; ok && index < list->count; index += 1)
        ok = strcmp(list->items[index].path, item->path) && strcmp(list->items[index].stored, item->stored);
    if (ok)
    {
        list->count += 1;
        list->bytes += size;
    }
    return ok;
}

/* Every evidence file the context names (bq_retirement_worker_evidence_sites),
 * in site order; any malformed, unprefixed or repeated descriptor, or more
 * than BQ_RETIREMENT_WORKER_EVIDENCE_CAP, refuses. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_evidence_list(BqRetirementWorkerBindingContext const* context,
    BqRetirementWorkerEvidenceList* list)
{
    *list = (BqRetirementWorkerEvidenceList){0};
    bool ok = context != NULL;
    static char const* const artifact_key[] = {"artifact"};
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(bq_retirement_worker_evidence_sites); index += 1)
    {
        BqRetirementWorkerEvidenceSite const* site = &bq_retirement_worker_evidence_sites[index];
        TpComposeJson const* json = &context->json[site->section];
        unsigned node = bq_retirement_worker_json_node(json, 0, site->path, site->depth);
        ok = node != TP_COMPOSE_JSON_NONE &&
             json->nodes[node].kind == (site->array ? TP_COMPOSE_JSON_ARRAY : TP_COMPOSE_JSON_OBJECT) &&
             (!site->array || json->nodes[node].first != TP_COMPOSE_JSON_NONE);
        if (ok && !site->array) ok = bq_retirement_worker_evidence_add(json, node, site, 0, list);
        u32 position = 0;
        for (unsigned child = ok && site->array ? json->nodes[node].first : TP_COMPOSE_JSON_NONE;
             ok && child != TP_COMPOSE_JSON_NONE; child = json->nodes[child].next)
        {
            unsigned item = site->array == BQ_RETIREMENT_WORKER_EVIDENCE_ARTIFACTS ?
                            bq_retirement_worker_json_node(json, child, artifact_key, 1) : child;
            ok = item != TP_COMPOSE_JSON_NONE && json->nodes[item].kind == TP_COMPOSE_JSON_OBJECT &&
                 bq_retirement_worker_evidence_add(json, item, site, position, list);
            position += 1;
        }
    }
    return ok;
}

/* The count and bytes of the pinned context's evidence files, for the store
 * plan before timing (the composition lists them again from the same pinned
 * bytes and requires the same totals). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_evidence_measure(Arena* arena, int installed, String8 profile,
    u32* entries, u64* bytes)
{
    BqRetirementWorkerBindingContext context = {0};
    BqRetirementWorkerEvidenceList* list = arena ? bq_retirement_worker_allocate(arena, 1, sizeof(*list)) : NULL;
    BqError result = list && entries && bytes ?
                     bq_retirement_worker_binding_import(arena, installed, profile, &context) : BQ_IO;
    if (result == BQ_OK && !bq_retirement_worker_evidence_list(&context, list)) result = BQ_RECIPE_MISMATCH;
    if (entries) *entries = result == BQ_OK ? list->count : 0;
    if (bytes) *bytes = result == BQ_OK ? list->bytes : 0;
    bq_retirement_worker_binding_release(&context);
    return result;
}

/* The installed evidence directory (BQ_RETIREMENT_WORKER_EVIDENCE_NAME under
 * recipes/): service-owned and not writable. */
BUSTER_GLOBAL_LOCAL int bq_retirement_worker_evidence_open(int installed)
{
    int recipes = installed >= 0 ? openat(installed, "recipes", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int directory = recipes >= 0 && bq_owned_directory(recipes, false, true) ?
                    openat(recipes, BQ_RETIREMENT_WORKER_EVIDENCE_NAME,
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (directory >= 0 && !bq_owned_directory(directory, false, true))
    {
        close(directory);
        directory = -1;
    }
    if (recipes >= 0) close(recipes);
    return directory;
}

/* A held binary's bytes (its descriptor, read without moving its offset):
 * a regular file of exactly `size` bytes. *output receives malloc'd memory. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_evidence_held(int descriptor, u64 size, u8** output)
{
    struct stat info = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) && (u64)info.st_size == size;
    u8* bytes = ok ? malloc((size_t)size) : NULL;
    ok = ok && bytes;
    u64 offset = 0;
    while (ok && offset < size)
    {
        ssize_t count = pread(descriptor, bytes + offset, (size_t)(size - offset), (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) offset += (u64)count;
    }
    if (!ok)
    {
        free(bytes);
        bytes = NULL;
    }
    *output = bytes;
    return ok;
}

/* One listed evidence file's bytes, from its held descriptor or the
 * installed evidence directory, when they are exactly the listed size and
 * SHA-256: BQ_OK with *output malloc'd, else BQ_SOURCE_MISMATCH (held) or
 * BQ_RECIPE_MISMATCH (installed) and nothing kept. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_evidence_read(BqRetirementWorkerEvidence const* item, int evidence,
    int const held[2], u8** output)
{
    char digest[SHA256_HEX_CAPACITY] = {0};
    u8* bytes = NULL;
    u32 length = 0;
    bool read = item->held ? bq_retirement_worker_evidence_held(held[item->held - 1u], item->bytes, &bytes) :
                bq_retirement_reference_read_installed(evidence, item->stored, (u32)BQ_WORKER_BUNDLE_FILE_CAP, &bytes,
                                                       &length, digest, NULL);
    if (read && item->held)
    {
        length = (u32)item->bytes;
        bq_digest((char const*)bytes, length, (char8*)digest);
    }
    BqError result = read && length == item->bytes && !strcmp(digest, item->sha256) ? BQ_OK :
                     item->held ? BQ_SOURCE_MISMATCH : BQ_RECIPE_MISMATCH;
    if (result != BQ_OK)
    {
        free(bytes);
        bytes = NULL;
    }
    *output = bytes;
    return result;
}

/* Each listed evidence file as a new, read-only result-root file under its
 * listed name: the subjects' binaries from the held descriptors (baseline,
 * candidate), every other file from the installed evidence directory. Every
 * source is read and verified first (bq_retirement_worker_evidence_read),
 * then each is read, verified again and written, so a refused source leaves
 * no evidence file: a changed installed file is BQ_RECIPE_MISMATCH, a changed
 * held binary BQ_SOURCE_MISMATCH, a write failure BQ_IO; on any refusal of
 * the second pass the files it wrote are unlinked again. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_evidence_publish(BqRetirementWorkerEvidenceList const* list,
    int evidence, int const held[2], int result_root)
{
    BqError result = list && held && evidence >= 0 && result_root >= 0 ? BQ_OK : BQ_IO;
    for (u32 index = 0; result == BQ_OK && index < list->count; index += 1)
    {
        u8* bytes = NULL;
        result = bq_retirement_worker_evidence_read(&list->items[index], evidence, held, &bytes);
        free(bytes);
    }
    u32 written = 0;
    for (u32 index = 0; result == BQ_OK && index < list->count; index += 1)
    {
        BqRetirementWorkerEvidence const* item = &list->items[index];
        u8* bytes = NULL;
        result = bq_retirement_worker_evidence_read(item, evidence, held, &bytes);
        if (result == BQ_OK &&
            !bq_retirement_worker_file_write(result_root, item->stored, (char const*)bytes, item->bytes, NULL))
            result = BQ_IO;
        free(bytes);
        written += result == BQ_OK;
    }
    for (u32 index = 0; result != BQ_OK && list && result_root >= 0 && index < written; index += 1)
        unlinkat(result_root, list->items[index].stored, 0);
    if (result != BQ_OK && written && result_root >= 0) fsync(result_root);
    return result;
}

/* ------------------------------------------------------------- composition */

/* The composer's request storage beyond lane D's handoff: the handoff's
 * arrays, the stream path lists and the binding. */
typedef struct BqRetirementWorkerCompose
{
    BqRetirementUnitHandoff handoff;
    TpRetirementComposeRequest request;
    TpRetirementComposeResult result;
    TpRetirementReceiptAuthority authority;
    TpRetirementComposeRow* rows;
    TpRetirementComposeCode* code;
    unsigned* group_kinds;
    char const** transcripts;
    char const** samples[2];
    char const** metrics;
    char const** untimed_metrics;
    char const** aa_transcripts;
    /* The binding's pre-replay closure beyond D's documents: the published
     * evidence and the A/A admission receipt. */
    TpRetirementComposeClosure closure[BQ_RETIREMENT_WORKER_EVIDENCE_CAP + 1u];
    char const* closure_stored[BQ_RETIREMENT_WORKER_EVIDENCE_CAP + 1u];
    char binding_sha256[SHA256_HEX_CAPACITY], admission_sha256[SHA256_HEX_CAPACITY];
    char label[TP_RETIREMENT_STORE_TOKEN_CAPACITY];
    u64 binding_bytes;
} BqRetirementWorkerCompose;

/* The first stream of a staged kind, then its spares: `used` store paths. */
BUSTER_GLOBAL_LOCAL char const** bq_retirement_worker_paths(Arena* arena, BqRetirementWorkerStreamGroup const* first,
    BqRetirementWorkerStreamGroup const* rest, unsigned used)
{
    char const** paths = used && first && first->count == 1 && rest && used - 1u <= rest->count ?
                         bq_retirement_worker_allocate(arena, used, sizeof(*paths)) : NULL;
    for (u32 index = 0; paths && index < used; index += 1)
        paths[index] = index ? rest->paths[index - 1u] : first->paths[0];
    return paths;
}

/* Lane D's handoff and every request field the caller owns: the store, the
 * scratch root, the reviewed adapter under the time left, the pre-timing
 * declaration, the binding, the published stream paths (A/B transcripts,
 * row and batch samples, A/B metrics, untimed records and metrics, the A/A
 * transcripts), D's documents as the prior closure and the sealed record. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_compose_request(BqRetirementWorkerCampaign* campaign,
    BqRetirementWorkerCompose* compose, char const* adapter, char const adapter_sha256[SHA256_HEX_CAPACITY],
    u64 deadline_ns)
{
    BqRetirementWorkerLayout const* layout = &campaign->layout;
    TpRetirementCampaignCapacity const* capacity = &campaign->campaign.capacity;
    BqRetirementUnitCampaign const* driver = &campaign->driver;
    TpRetirementComposeRequest* request = &compose->request;
    compose->rows = bq_retirement_worker_allocate(campaign->arena, layout->rows, sizeof(*compose->rows));
    compose->group_kinds = bq_retirement_worker_allocate(campaign->arena, layout->groups, sizeof(*compose->group_kinds));
    compose->code = bq_retirement_worker_allocate(campaign->arena, campaign->code_capacity + 1u, sizeof(*compose->code));
    bool ok = compose->rows && compose->group_kinds && compose->code &&
              bq_retirement_unit_handoff(driver, campaign->timed_rows, layout->rows, compose->rows, compose->group_kinds,
                                         layout->groups, compose->code, campaign->code_capacity + 1u, &compose->handoff,
                                         request) &&
              bq_retirement_unit_handoff_retained_matches(&compose->handoff, campaign->declaration.retained,
                  BQ_RETIREMENT_WORKER_RETAINED, BQ_RETIREMENT_WORKER_RETAINED - BQ_RETIREMENT_UNIT_HANDOFF_RETAINED);
    u64 rows = tp_retirement_campaign_ceil_div(capacity->row_samples_per_stage, TP_RETIREMENT_SAMPLE_SHARD_RECORDS);
    BqRetirementWorkerStreamGroup const* samples = &campaign->samples[1];
    ok = ok && rows && rows < samples->count;
    compose->transcripts = ok ? bq_retirement_worker_paths(campaign->arena, &campaign->transcripts_first[1],
                                                           &campaign->transcripts[1], driver->transcript_shards[1]) : NULL;
    compose->aa_transcripts = ok ? bq_retirement_worker_paths(campaign->arena, &campaign->transcripts_first[0],
                                                              &campaign->transcripts[0], driver->transcript_shards[0]) : NULL;
    compose->metrics = ok ? bq_retirement_worker_paths(campaign->arena, &campaign->metrics_first[1], &campaign->metrics[1],
                                                       driver->metrics_shards[2]) : NULL;
    compose->samples[0] = ok ? bq_retirement_worker_allocate(campaign->arena, rows, sizeof(char const*)) : NULL;
    compose->samples[1] = ok ? bq_retirement_worker_allocate(campaign->arena, samples->count - rows,
                                                             sizeof(char const*)) : NULL;
    unsigned untimed = driver->metrics_shards[0];
    compose->untimed_metrics = ok && untimed ? bq_retirement_worker_allocate(campaign->arena, untimed,
                                                                             sizeof(char const*)) : NULL;
    ok = ok && compose->transcripts && compose->aa_transcripts && compose->metrics && compose->samples[0] &&
         compose->samples[1] && (!untimed || (compose->untimed_metrics && untimed <= campaign->untimed_shards.count));
    for (u32 index = 0; ok && index < samples->count; index += 1)
        compose->samples[index < rows ? 0 : 1][index < rows ? index : index - rows] = samples->paths[index];
    for (u32 index = 0; ok && index < untimed; index += 1)
        compose->untimed_metrics[index] = campaign->untimed_shards.paths[index];
    u64 now = tp_process_monotonic_ns();
    if (ok)
    {
        request->store = &campaign->store;
        request->scratch_root = campaign->directories[BQ_RETIREMENT_WORKER_SCRATCH];
        request->adapter_path = adapter;
        request->adapter_sha256 = adapter_sha256;
        request->adapter_timeout_ns = deadline_ns > now && deadline_ns - now < TP_RETIREMENT_COMPOSE_ADAPTER_TIMEOUT_NS ?
                                      deadline_ns - now : 0;
        request->declaration = &campaign->declaration.declaration;
        request->binding_path = BQ_RETIREMENT_WORKER_BINDING_PATH;
        request->binding_sha256 = compose->binding_sha256;
        request->transcript_paths = compose->transcripts;
        request->transcript_count = driver->transcript_shards[1];
        request->sample_paths[0] = compose->samples[0];
        request->sample_counts[0] = (unsigned)rows;
        request->sample_paths[1] = compose->samples[1];
        request->sample_counts[1] = samples->count - (unsigned)rows;
        request->metrics_paths = compose->metrics;
        request->metrics_count = driver->metrics_shards[2];
        request->untimed_path = BQ_RETIREMENT_WORKER_UNTIMED_PATH;
        request->untimed_metrics_paths = compose->untimed_metrics;
        request->untimed_metrics_count = untimed;
        request->aa_transcript_paths = compose->aa_transcripts;
        request->aa_transcript_count = driver->transcript_shards[0];
        request->prior = compose->handoff.prior;
        request->prior_count = BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS;
        request->sealed_path = BQ_RETIREMENT_WORKER_SEALED_PATH;
    }
    ok = ok && deadline_ns > now;
    return ok;
}

/* The values the context chain carries for the coordinator: the bind time,
 * lane D's pre-sample and post-sample contexts, the three log chains, both
 * stages' facts and the binding's digest. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_chain_carried(BqRetirementWorkerCampaign const* campaign,
    char const binding_sha256[SHA256_HEX_CAPACITY], BqRetirementContextChainCarried* carried)
{
    BqRetirementUnitCampaign const* driver = &campaign->driver;
    *carried = (BqRetirementContextChainCarried){.bound_at_ns = campaign->campaign.bound_at_ns};
    snprintf(carried->pre_sample, sizeof(carried->pre_sample), "%s", driver->context_sha256);
    snprintf(carried->post_sample, sizeof(carried->post_sample), "%s", driver->post_context_sha256);
    snprintf(carried->binding, sizeof(carried->binding), "%s", binding_sha256);
    memcpy(carried->logs, driver->log_chain_sha256, sizeof(carried->logs));
    for (u32 stage = 0; stage < TP_RETIREMENT_CAMPAIGN_STAGES; stage += 1)
        bq_retirement_unit_campaign_stage_facts(&campaign->stages[stage].samples, driver->shard_chain_sha256[stage],
                                                &carried->stages[stage]);
    bool ok = carried->bound_at_ns && tp_retirement_digest(carried->pre_sample) &&
              tp_retirement_digest(carried->post_sample) && tp_retirement_digest(carried->binding);
    return ok;
}

/* The producer authority in the attempt's new private retirement-authority/
 * (the result directory is the store root), then the context chain beside
 * it: A, the ready digest sent, the row-plan pin, the authority's plan and
 * context and the carried values. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_authority_publish(BqRetirementWorkerCampaign* campaign,
    BqRetirementWorkerCompose* compose, BqJob const* job, String8 profile,
    char const preparation_sha256[SHA256_HEX_CAPACITY], char const ready_sha256[SHA256_HEX_CAPACITY])
{
    char row_plan[SHA256_HEX_CAPACITY] = {0}, chain[BQ_RETIREMENT_CONTEXT_CHAIN_CAP];
    char name[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    BqRetirementContextChainCarried carried;
    bool made = campaign->attempt >= 0 && mkdirat(campaign->attempt, BQ_RETIREMENT_UNIT_AUTHORITY_DIRECTORY, 0700) == 0;
    int authority = made ? bq_retirement_unit_promote(openat(campaign->attempt, BQ_RETIREMENT_UNIT_AUTHORITY_DIRECTORY,
                                                             O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) : -1;
    BqError result = authority >= 3 && bq_owned_directory(authority, true, false) && fsync(campaign->attempt) == 0 ?
                     BQ_OK : BQ_WORKSPACE_MISMATCH;
    if (result == BQ_OK &&
        !(tp_retirement_store_job_label(compose->label, job->id) &&
          tp_retirement_store_receipt_authority(&campaign->store, authority, TP_RETIREMENT_EXECUTION_RECEIPT_PATH,
              compose->label, job->token, compose->request.execution_plan_sha256, compose->result.context_sha256,
              &compose->authority)))
        result = BQ_CORRUPT;
    u32 length = result == BQ_OK && bq_retirement_profile_sha(profile, S8("row-plan-sha256="), row_plan) &&
                 bq_retirement_worker_chain_carried(campaign, compose->binding_sha256, &carried) ?
                 bq_retirement_context_chain_format(chain, job->id, job->token, preparation_sha256, ready_sha256,
                     row_plan, compose->authority.plan_sha256, compose->authority.context_sha256, &carried) : 0;
    if (result == BQ_OK &&
        !(length && bq_retirement_context_chain_name(name, job->id, job->token) &&
          bq_retirement_worker_file_write(authority, name, chain, length, NULL)))
        result = BQ_IO;
    if (authority >= 0 && close(authority) != 0 && result == BQ_OK) result = BQ_IO;
    return result;
}

/* ------------------------------------------------------ the result's index */

/* One regular file of the result root. */
typedef struct BqRetirementWorkerBundleEntry
{
    char path[BQ_WORKER_BUNDLE_PATH_CAP + 1];
    char digest[SHA256_HEX_CAPACITY];
    u64 size;
} BqRetirementWorkerBundleEntry;

/* A regular, single-link, service-owned file's size and SHA-256, read from
 * the same inode it was listed as, in 64 KiB reads: at most its own per-file
 * cap (bq_worker_bundle_file_cap; a retirement evidence entry has the larger
 * evidence cap, #1880). */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_bundle_hash(int directory, char const* name, struct stat const* listed,
    BqRetirementWorkerBundleEntry* entry)
{
    int file = openat(directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    bool ok = file >= 0 && fstat(file, &info) == 0 && info.st_dev == listed->st_dev && info.st_ino == listed->st_ino &&
              S_ISREG(info.st_mode) && info.st_nlink == 1 && info.st_uid == geteuid() && !(info.st_mode & 022) &&
              (u64)info.st_size <= bq_worker_bundle_file_cap(true, name);
    Sha256 hash;
    sha256_init(&hash);
    u8 buffer[65536];
    u64 total = 0;
    ssize_t count = 1;
    while (ok && count > 0)
    {
        count = read(file, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) count = 1;
        else if (count < 0) ok = false;
        else if (count > 0)
        {
            sha256_add(&hash, buffer, (u64)count);
            total += (u64)count;
        }
    }
    ok = ok && total == (u64)info.st_size;
    if (file >= 0 && close(file) != 0) ok = false;
    if (ok)
    {
        sha256_finish_hex(&hash, (char8*)entry->digest);
        entry->size = total;
    }
    return ok;
}

/* The BQ-BUNDLE-V1 index of every regular file in the (flat) result root
 * except the recipe's three control files, in path order, as the
 * coordinator's bq_worker_bundle_validate_recipe requires; any other entry
 * type refuses. Published new and read-only; digest receives its SHA-256. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_bundle_write(Arena* arena, int result_root, BqRecipeFiles const* recipe,
    char digest[SHA256_HEX_CAPACITY])
{
    BqRetirementWorkerBundleEntry* entries = bq_retirement_worker_allocate(arena, BQ_WORKER_BUNDLE_ENTRY_CAP,
                                                                            sizeof(*entries));
    u32* order = bq_retirement_worker_allocate(arena, BQ_WORKER_BUNDLE_ENTRY_CAP, sizeof(*order));
    char* body = bq_retirement_worker_allocate(arena, BQ_WORKER_BUNDLE_CAP, 1);
    int listing = result_root >= 0 ? openat(result_root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    DIR* stream = listing >= 0 ? fdopendir(listing) : NULL;
    if (!stream && listing >= 0) close(listing);
    bool ok = entries && order && body && recipe && stream;
    u32 count = 0;
    u64 total = 0;
    struct dirent* item = NULL;
    errno = 0;
    while (ok && (item = readdir(stream)) != NULL)
    {
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..") || !strcmp(item->d_name, recipe->manifest) ||
            !strcmp(item->d_name, recipe->bundle) || !strcmp(item->d_name, recipe->outcome))
            continue;
        struct stat listed = {0};
        size_t length = strlen(item->d_name);
        ok = count < BQ_WORKER_BUNDLE_ENTRY_CAP - BQ_WORKER_BUNDLE_CONTROL_ENTRIES && length &&
             length <= BQ_WORKER_BUNDLE_PATH_CAP && fstatat(result_root, item->d_name, &listed, AT_SYMLINK_NOFOLLOW) == 0 &&
             S_ISREG(listed.st_mode) && bq_retirement_worker_bundle_hash(result_root, item->d_name, &listed, entries + count) &&
             total <= BQ_WORKER_RETIREMENT_BUNDLE_TOTAL_CAP - entries[count].size;
        for (size_t index = 0; ok && index < length; index += 1)
            ok = (u8)item->d_name[index] >= 0x21 && (u8)item->d_name[index] <= 0x7e && item->d_name[index] != '\\';
        if (ok)
        {
            memcpy(entries[count].path, item->d_name, length + 1u);
            total += entries[count].size;
            order[count] = count;
            count += 1;
        }
        errno = 0;
    }
    ok = ok && errno == 0;
    if (stream) closedir(stream);
    /* Path order (insertion sort over indexes). */
    for (u32 index = 1; ok && index < count; index += 1)
    {
        u32 moving = order[index], slot = index;
        while (slot && strcmp(entries[order[slot - 1]].path, entries[moving].path) > 0)
        {
            order[slot] = order[slot - 1];
            slot -= 1;
        }
        order[slot] = moving;
    }
    int header = ok ? snprintf(body, BQ_WORKER_BUNDLE_CAP, "BQ-BUNDLE-V1\nentries=%u\nbytes=%" PRIu64 "\n", count,
                               total) : -1;
    u64 used = header > 0 ? (u64)header : 0;
    ok = ok && header > 0;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqRetirementWorkerBundleEntry const* entry = entries + order[index];
        int line = snprintf(body + used, BQ_WORKER_BUNDLE_CAP - used, "%s %" PRIu64 " %s\n", entry->digest,
                            entry->size, entry->path);
        ok = line > 0 && (u64)line < BQ_WORKER_BUNDLE_CAP - used;
        used += ok ? (u64)line : 0;
    }
    ok = ok && bq_retirement_worker_file_write(result_root, recipe->bundle, body, used, digest);
    return ok;
}

/* The retirement manifest, the one spelling the producer writes and the
 * coordinator re-formats (bq_worker_result_validate's retirement branch):
 * BQ_RETIREMENT_WORKER_MANIFEST_PREFIX, then the job, attempt and roots, the
 * ready and authority digests the channel carried, the execution receipt's
 * digest, the sealed result's and the binding's paths and digests, and the
 * bundle index's digest. Returns the length, or -1. */
BUSTER_GLOBAL_LOCAL int bq_retirement_worker_manifest_format(char* output, size_t capacity, u64 job_id,
    u64 attempt_token, String8 workspace_root, char const* result_root, char const* ready_sha256,
    char const* authority_sha256, char const* receipt_sha256, char const* sealed_sha256, char const* binding_sha256,
    char const* bundle_sha256)
{
    char const* digests[] = {ready_sha256, authority_sha256, receipt_sha256, sealed_sha256, binding_sha256,
                             bundle_sha256};
    bool ok = output && workspace_root.length && workspace_root.length <= BQ_PATH_CAP && result_root &&
              result_root[0] == '/';
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(digests); index += 1)
        ok = digests[index] && tp_retirement_digest(digests[index]);
    int length = ok ? snprintf(output, capacity, BQ_RETIREMENT_WORKER_MANIFEST_PREFIX "job-id=%" PRIu64
        "\nattempt-token=%" PRIu64 "\nworkspace-root=%.*s\nresult-root=%s\nready-sha256=%s\nauthority-sha256=%s"
        "\nreceipt-sha256=%s\nsealed-result=" BQ_RETIREMENT_WORKER_SEALED_PATH "\nsealed-result-sha256=%s\nbinding="
        BQ_RETIREMENT_WORKER_BINDING_PATH "\nbinding-sha256=%s\nbundle-sha256=%s\n", (uint64_t)job_id,
        (uint64_t)attempt_token, (int)workspace_root.length, (char const*)workspace_root.pointer, result_root,
        ready_sha256, authority_sha256, receipt_sha256, sealed_sha256, binding_sha256, bundle_sha256) : -1;
    int result = length > 0 && (size_t)length < capacity ? length : -1;
    return result;
}

/* After MEASURED: the bundle index over the result root (the coordinator's
 * worker-phase receipts included), then the retirement manifest naming it. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_result_write(BqRetirementWorkerCampaign* campaign,
    BqRetirementWorkerCompose const* compose, int result_root, BqJob const* job, String8 workspace_root,
    char const* result_path, char const ready_sha256[SHA256_HEX_CAPACITY])
{
    BqRecipeFiles recipe;
    char bundle[SHA256_HEX_CAPACITY] = {0}, manifest[2048];
    bool ok = bq_recipe_files(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED, &recipe) &&
              bq_retirement_worker_bundle_write(campaign->arena, result_root, &recipe, bundle);
    int length = ok ? bq_retirement_worker_manifest_format(manifest, sizeof(manifest), job->id, job->token,
        workspace_root, result_path, ready_sha256, compose->authority.authority_sha256, compose->authority.receipt_sha256,
        compose->result.sealed.sha256, compose->binding_sha256, bundle) : -1;
    ok = ok && length > 0 && bq_retirement_worker_file_write(result_root, recipe.manifest, manifest, (u64)length, NULL);
    return ok;
}

/* ------------------------------------------------------------------- entry */

/* After lane D's READY (the post-sample record published): the handoff, the
 * admission receipt and binding, the composer, the authority and its chain,
 * MEASURED and, once it is acknowledged, the result's index and manifest.
 * Returns BQ_OK only after the manifest is written. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_compose(BqRetirementWorkerCampaign* campaign,
    BqRetirementCampaignUnitStore const* unit, BqJob const* job, BqRetirementUnitGate const* unit_gate, int result_root,
    char const* result_path, char const* adapter, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const ready_sha256[SHA256_HEX_CAPACITY], u64 deadline_ns)
{
    BqRetirementWorkerCompose* compose = bq_retirement_worker_allocate(campaign->arena, 1, sizeof(*compose));
    BqRetirementWorkerBindingContext context = {0};
    char adapter_sha256[SHA256_HEX_CAPACITY] = {0};
    BqError result = compose && campaign->driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_READY && adapter &&
                     adapter[0] == '/' ? BQ_OK : BQ_BAD_REQUEST;
    if (result == BQ_OK && !bq_retirement_profile_sha(unit->profile, S8("adapter-sha256="), adapter_sha256))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK && !bq_retirement_worker_compose_request(campaign, compose, adapter, adapter_sha256, deadline_ns))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK)
        result = bq_retirement_worker_binding_import(campaign->arena, unit->installed, unit->profile, &context);
    if (result == BQ_OK &&
        !bq_retirement_worker_binding_check(&context, unit->profile, &unit_gate->correctness,
                                            campaign->driver.family_sha256, &campaign->pins, &campaign->driver.plan))
        result = BQ_RECIPE_MISMATCH;
    if (result == BQ_OK &&
        !(bq_retirement_worker_admission_write(result_root, campaign->aa_receipt, campaign->aa_receipt_bytes,
                                               campaign->driver.aa_admission_sha256, compose->admission_sha256) &&
          bq_retirement_worker_binding_write(result_root, &context, campaign->driver.documents,
                                             campaign->aa_receipt_bytes, compose->admission_sha256, campaign->arena,
                                             &compose->binding_bytes, compose->binding_sha256)))
        result = BQ_IO;
    /* The evidence the binding names, the totals the store plan reserved. */
    BqRetirementWorkerEvidenceList* evidence = compose ? bq_retirement_worker_allocate(campaign->arena, 1,
                                                                                       sizeof(*evidence)) : NULL;
    if (result == BQ_OK &&
        !(evidence && bq_retirement_worker_evidence_list(&context, evidence) &&
          evidence->count == campaign->evidence_entries && evidence->bytes == campaign->evidence_bytes))
        result = BQ_RECIPE_MISMATCH;
    int evidence_directory = result == BQ_OK ? bq_retirement_worker_evidence_open(unit->installed) : -1;
    if (result == BQ_OK && evidence_directory < 0) result = BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK)
        result = bq_retirement_worker_evidence_publish(evidence, evidence_directory, campaign->held.descriptors,
                                                       result_root);
    if (evidence_directory >= 0 && close(evidence_directory) != 0 && result == BQ_OK) result = BQ_IO;
    /* The composer seals them with D's documents: the validator's closure. */
    for (u32 index = 0; result == BQ_OK && index < evidence->count; index += 1)
    {
        compose->closure[index] = (TpRetirementComposeClosure){evidence->items[index].name,
            evidence->items[index].path, evidence->items[index].bytes, evidence->items[index].sha256};
        compose->closure_stored[index] = evidence->items[index].stored;
    }
    if (result == BQ_OK)
    {
        compose->closure[evidence->count] = (TpRetirementComposeClosure){"execution.host.aa_admission_receipt",
            BQ_RETIREMENT_WORKER_ADMISSION_PATH, campaign->aa_receipt_bytes, compose->admission_sha256};
        compose->closure_stored[evidence->count] = BQ_RETIREMENT_WORKER_ADMISSION_PATH;
        compose->request.closure = compose->closure;
        compose->request.closure_stored = compose->closure_stored;
        compose->request.closure_count = evidence->count + 1u;
    }
    if (result == BQ_OK) result = bq_retirement_unit_stop_reason(campaign->driver.cancellation_fd, deadline_ns, BQ_OK);
    if (result == BQ_OK && !tp_retirement_compose(&compose->request, &compose->result))
    {
        if (compose->result.refused)
            fprintf(stderr, "retirement worker-unit: composer refused at %s\n", compose->result.refused);
        result = BQ_CORRUPT;
    }
    if (result == BQ_OK)
        result = bq_retirement_worker_authority_publish(campaign, compose, job, unit->profile, preparation_sha256,
                                                        ready_sha256);
    campaign->composed = result == BQ_OK;
    BqRetirementUnitCampaignHandoff confirmed = {compose ? compose->result.sealed.sha256 : NULL,
                                                 compose ? compose->authority.authority_sha256 : NULL};
    if (result == BQ_OK && !bq_retirement_unit_campaign_measured(&campaign->driver, &confirmed)) result = BQ_WORKER_MISMATCH;
    if (result == BQ_OK &&
        !bq_retirement_worker_result_write(campaign, compose, result_root, job, unit->workspace_root, result_path,
                                           ready_sha256))
        result = BQ_IO;
    bq_retirement_worker_binding_release(&context);
    return result;
}
