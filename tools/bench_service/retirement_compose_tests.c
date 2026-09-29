/* #881-E composer fixtures and command-line driver (Linux only).
 *
 * Link with retirement_compose.c, retirement_result.c (built with
 * BUSTER_RETIREMENT_STORE_TEST) and tools/throughput/shared.c. Three modes:
 *   (no arguments)                 native fail-closed fixtures, printed as
 *                                  `COMPOSE_TEST assertions=N failures=N`
 *   compose SPEC                   plan a fresh store, import lane D's stream
 *                                  files, compose, optionally issue the
 *                                  producer authority, print COMPOSE_RESULT
 *   retirement-replay --input I --output O
 *                                  a stub adapter for the native fixtures only
 * The Python end-to-end test (retirement_compose_test.py) drives `compose`
 * with the reviewed adapter and runs the binding validator on the output.
 * No fixture here is service admission or performance evidence.
 *
 * Map: Driver, driver_parse, driver_plan, driver_import, driver_compose,
 * fixture_generate, test_compose_success, test_compose_refusals,
 * test_budget_and_settle, test_handoff.
 */
#define _GNU_SOURCE 1
#include "retirement_compose.h"
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern unsigned tp_retirement_store_test_sync_calls, tp_retirement_store_test_fail_sync;
static unsigned assertions, failures;
#define CHECK(value) do { ++assertions; if (!(value)) { ++failures; fprintf(stderr, "COMPOSE_TEST line=%d: %s\n", __LINE__, #value); } } while (0)

#define DRIVER_ROWS 64u
#define DRIVER_GROUPS 64u
#define DRIVER_FILES 64u
#define DRIVER_PRIOR 128u
#define DRIVER_PATH 512u

typedef struct DriverFile
{
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char source[DRIVER_PATH];
} DriverFile;

typedef struct DriverList
{
    DriverFile files[DRIVER_FILES];
    char const* paths[DRIVER_FILES];
    unsigned count;
} DriverList;

typedef struct DriverPrior
{
    char name[TP_RETIREMENT_COMPOSE_NAME_BYTES + 1];
    char path[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char sha256[65];
    uint64_t bytes;
} DriverPrior;

typedef struct Driver
{
    char source[DRIVER_PATH], store_path[DRIVER_PATH], evidence[DRIVER_PATH], scratch[DRIVER_PATH];
    char adapter[DRIVER_PATH], authority_path[DRIVER_PATH], context_path[DRIVER_PATH];
    char job[129], boot[129], sealed[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char digests[5][65];
    char dimensions[DRIVER_ROWS][TP_RETIREMENT_COMPOSE_DIMENSIONS][65];
    int source_fd, store_fd, evidence_fd, scratch_fd, authority_fd;
    uint64_t attempt, bound_at_ns, completed_at_ns, metrics_header, metrics_input;
    TpRetirementComposeRow rows[DRIVER_ROWS];
    unsigned group_kinds[DRIVER_GROUPS], group_inputs[DRIVER_GROUPS], group_count, row_count;
    unsigned untimed_kinds[DRIVER_GROUPS], untimed_inputs[DRIVER_GROUPS], untimed_count, population_rows;
    TpRetirementComposeCode code[DRIVER_ROWS];
    unsigned code_count;
    TpRetirementComposePartition partitions[2][TP_RETIREMENT_COMPOSE_PARTITIONS];
    unsigned partition_counts[2];
    DriverList transcript, samples[2], metrics, untimed, untimed_metrics, retained;
    DriverPrior prior_storage[DRIVER_PRIOR];
    TpRetirementComposeClosure prior[DRIVER_PRIOR];
    unsigned prior_count;
    unsigned char* context;
    size_t context_bytes;
    TpRetirementPlan statistics;
    TpRetirementComposeLayout layout;
    TpRetirementStore store;
    TpRetirementStoredFile* files;
    TpRetirementComposeRequest request;
    TpRetirementComposeResult result;
    TpRetirementReceiptAuthority authority;
} Driver;

static void driver_init(Driver* driver)
{
    memset(driver, 0, sizeof(*driver));
    driver->source_fd = driver->store_fd = driver->evidence_fd = driver->scratch_fd = driver->authority_fd = -1;
    driver->store.root = -1;
}

static void driver_close(Driver* driver)
{
    tp_retirement_store_close(&driver->store);
    int* descriptors[] = {&driver->source_fd, &driver->store_fd, &driver->evidence_fd, &driver->scratch_fd,
                          &driver->authority_fd};
    for (unsigned i = 0; i < sizeof(descriptors) / sizeof(descriptors[0]); ++i)
        if (*descriptors[i] >= 0)
        {
            close(*descriptors[i]);
            *descriptors[i] = -1;
        }
    free(driver->files);
    driver->files = NULL;
    free(driver->context);
    driver->context = NULL;
}

static int driver_directory(char const* path)
{
    int fd = path && path[0] ? open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
    return fd;
}

static int driver_list_add(DriverList* list, char const* path, char const* source)
{
    int valid = list->count < DRIVER_FILES && path && strlen(path) <= TP_RETIREMENT_STORE_PATH_BYTES &&
                (!source || strlen(source) < DRIVER_PATH);
    if (valid)
    {
        DriverFile* file = list->files + list->count;
        strcpy(file->path, path);
        strcpy(file->source, source ? source : path);
        list->paths[list->count] = file->path;
        ++list->count;
    }
    return valid;
}

static int driver_list_remove(DriverList* list, char const* path)
{
    unsigned found = list->count;
    for (unsigned i = 0; i < list->count; ++i)
        if (!strcmp(list->files[i].path, path)) found = i;
    int valid = found < list->count;
    for (unsigned i = found; valid && i + 1 < list->count; ++i)
    {
        list->files[i] = list->files[i + 1];
        list->paths[i] = list->files[i].path;
    }
    if (valid) --list->count;
    return valid;
}

static int driver_kind(char const* text, unsigned* kind)
{
    int valid = text && (!strcmp(text, "object") || !strcmp(text, "singleton"));
    if (valid) *kind = !strcmp(text, "object") ? TP_RETIREMENT_GROUP_OBJECT : TP_RETIREMENT_GROUP_SINGLETON;
    return valid;
}

static int driver_digest_copy(char output[65], char const* text)
{
    int valid = text && strlen(text) == 64;
    if (valid) strcpy(output, text);
    return valid;
}

/* One whitespace-separated directive per line; see the file header of
 * retirement_compose_test.py for the directives it writes. */
static int driver_parse(Driver* driver, char const* spec)
{
    FILE* input = fopen(spec, "rb");
    int valid = input != NULL;
    char line[4096];
    while (valid && fgets(line, sizeof(line), input))
    {
        char* words[16] = {0};
        unsigned count = 0;
        for (char* token = strtok(line, " \t\r\n"); token && count < 16; token = strtok(NULL, " \t\r\n"))
            words[count++] = token;
        char const* key = count ? words[0] : "";
        if (!count) continue;
        if (!strcmp(key, "source") && count == 2) valid = strlen(words[1]) < DRIVER_PATH && strcpy(driver->source, words[1]);
        else if (!strcmp(key, "store") && count == 2) valid = strlen(words[1]) < DRIVER_PATH && strcpy(driver->store_path, words[1]);
        else if (!strcmp(key, "evidence") && count == 2) valid = strlen(words[1]) < DRIVER_PATH && strcpy(driver->evidence, words[1]);
        else if (!strcmp(key, "scratch") && count == 2) valid = strlen(words[1]) < DRIVER_PATH && strcpy(driver->scratch, words[1]);
        else if (!strcmp(key, "adapter") && count == 2) valid = strlen(words[1]) < DRIVER_PATH && strcpy(driver->adapter, words[1]);
        else if (!strcmp(key, "authority") && count == 2)
            valid = strlen(words[1]) < DRIVER_PATH && strcpy(driver->authority_path, words[1]);
        else if (!strcmp(key, "context") && count == 2)
            valid = strlen(words[1]) < DRIVER_PATH && strcpy(driver->context_path, words[1]);
        else if (!strcmp(key, "sealed") && count == 2)
            valid = strlen(words[1]) <= TP_RETIREMENT_STORE_PATH_BYTES && strcpy(driver->sealed, words[1]);
        else if (!strcmp(key, "identity") && count == 6)
        {
            valid = strlen(words[1]) <= 128 && strlen(words[3]) <= 128;
            if (valid)
            {
                strcpy(driver->job, words[1]);
                strcpy(driver->boot, words[3]);
                driver->attempt = strtoull(words[2], NULL, 10);
                driver->bound_at_ns = strtoull(words[4], NULL, 10);
                driver->completed_at_ns = strtoull(words[5], NULL, 10);
            }
        }
        else if (!strcmp(key, "digests") && count == 6)
            for (unsigned i = 0; valid && i < 5; ++i) valid = driver_digest_copy(driver->digests[i], words[i + 1]);
        else if (!strcmp(key, "statistics") && count == 6)
            driver->statistics = (TpRetirementPlan){.seed = strtoull(words[1], NULL, 10),
                .version = TP_RETIREMENT_STATISTICS_VERSION, .pairs_per_round = (unsigned)strtoul(words[2], NULL, 10),
                .resamples = (unsigned)strtoul(words[3], NULL, 10),
                .bootstrap_members_per_scope = (unsigned)strtoul(words[4], NULL, 10),
                .cell_members_per_scope = (unsigned)strtoul(words[5], NULL, 10), .frozen_before_samples = 1};
        else if (!strcmp(key, "population") && count == 2) driver->population_rows = (unsigned)strtoul(words[1], NULL, 10);
        else if (!strcmp(key, "metrics-budget") && count == 3)
        {
            driver->metrics_header = strtoull(words[1], NULL, 10);
            driver->metrics_input = strtoull(words[2], NULL, 10);
        }
        else if (!strcmp(key, "group") && count == 3)
        {
            valid = driver->group_count < DRIVER_GROUPS &&
                    driver_kind(words[1], &driver->group_kinds[driver->group_count]);
            if (valid) driver->group_inputs[driver->group_count++] = (unsigned)strtoul(words[2], NULL, 10);
        }
        else if (!strcmp(key, "untimed-group") && count == 3)
        {
            valid = driver->untimed_count < DRIVER_GROUPS &&
                    driver_kind(words[1], &driver->untimed_kinds[driver->untimed_count]);
            if (valid) driver->untimed_inputs[driver->untimed_count++] = (unsigned)strtoul(words[2], NULL, 10);
        }
        else if (!strcmp(key, "row") && count == 10)
        {
            unsigned index = driver->row_count;
            valid = index < DRIVER_ROWS;
            for (unsigned d = 0; valid && d < TP_RETIREMENT_COMPOSE_DIMENSIONS; ++d)
            {
                valid = strlen(words[4 + d]) < 65;
                if (valid) strcpy(driver->dimensions[index][d], words[4 + d]);
            }
            if (valid)
            {
                TpRetirementComposeRow* row = driver->rows + index;
                row->id = (unsigned)strtoul(words[1], NULL, 10);
                row->group = (unsigned)strtoul(words[2], NULL, 10);
                row->runtime = (unsigned)strtoul(words[3], NULL, 10);
                for (unsigned d = 0; d < TP_RETIREMENT_COMPOSE_DIMENSIONS; ++d)
                    row->dimensions[d] = driver->dimensions[index][d];
                ++driver->row_count;
            }
        }
        else if (!strcmp(key, "code") && count == 10)
        {
            valid = driver->code_count < DRIVER_ROWS;
            TpRetirementComposeCode* code = valid ? driver->code + driver->code_count : NULL;
            if (valid) code->row = (unsigned)strtoul(words[1], NULL, 10);
            for (unsigned side = 0; valid && side < 2; ++side)
            {
                TpRetirementCodeSide* entry = code->sides + side;
                valid = driver_digest_copy(entry->artifact_sha256, words[2 + side * 4]) &&
                        driver_digest_copy(entry->code_sha256, words[4 + side * 4]) &&
                        driver_digest_copy(entry->reproduction_sha256, words[5 + side * 4]);
                entry->code_bytes = strtoull(words[3 + side * 4], NULL, 10);
            }
            if (valid) ++driver->code_count;
        }
        else if (!strcmp(key, "partition") && count == 6)
        {
            unsigned population = !strcmp(words[1], "batches");
            unsigned index = driver->partition_counts[population];
            valid = (population || !strcmp(words[1], "rows")) && index < TP_RETIREMENT_COMPOSE_PARTITIONS &&
                    strlen(words[2]) <= TP_RETIREMENT_COMPOSE_NAME_BYTES && strlen(words[3]) <= TP_RETIREMENT_STORE_PATH_BYTES;
            if (valid)
            {
                TpRetirementComposePartition* partition = driver->partitions[population] + index;
                strcpy(partition->identity, words[2]);
                strcpy(partition->path, words[3]);
                partition->start = strtoull(words[4], NULL, 10);
                partition->records = strtoull(words[5], NULL, 10);
                ++driver->partition_counts[population];
            }
        }
        else if (!strcmp(key, "transcript") && (count == 2 || count == 3))
            valid = driver_list_add(&driver->transcript, words[1], count == 3 ? words[2] : NULL);
        else if (!strcmp(key, "samples") && (count == 3 || count == 4))
            valid = (!strcmp(words[1], "rows") || !strcmp(words[1], "batches")) &&
                    driver_list_add(&driver->samples[!strcmp(words[1], "batches")], words[2], count == 4 ? words[3] : NULL);
        else if (!strcmp(key, "metrics") && (count == 2 || count == 3))
            valid = driver_list_add(&driver->metrics, words[1], count == 3 ? words[2] : NULL);
        else if (!strcmp(key, "untimed") && (count == 2 || count == 3))
            valid = !driver->untimed.count && driver_list_add(&driver->untimed, words[1], count == 3 ? words[2] : NULL);
        else if (!strcmp(key, "untimed-metrics") && (count == 2 || count == 3))
            valid = driver_list_add(&driver->untimed_metrics, words[1], count == 3 ? words[2] : NULL);
        else if (!strcmp(key, "retained") && (count == 2 || count == 3))
            valid = driver_list_add(&driver->retained, words[1], count == 3 ? words[2] : NULL);
        else if (!strcmp(key, "prior") && count == 5)
        {
            DriverPrior* prior = driver->prior_count < DRIVER_PRIOR ? driver->prior_storage + driver->prior_count : NULL;
            valid = prior && strlen(words[1]) <= TP_RETIREMENT_COMPOSE_NAME_BYTES &&
                    strlen(words[2]) <= TP_RETIREMENT_STORE_PATH_BYTES && driver_digest_copy(prior->sha256, words[4]);
            if (valid)
            {
                strcpy(prior->name, words[1]);
                strcpy(prior->path, words[2]);
                prior->bytes = strtoull(words[3], NULL, 10);
                ++driver->prior_count;
            }
        }
        else valid = 0;
        if (!valid) fprintf(stderr, "COMPOSE_SPEC rejected directive: %s\n", key);
    }
    if (input && fclose(input) != 0) valid = 0;
    return valid;
}

static int driver_file_size(int root, char const* path, uint64_t* bytes)
{
    struct stat info = {0};
    int valid = fstatat(root, path, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
    *bytes = valid ? (uint64_t)info.st_size : 0;
    return valid;
}

/* Reserve the store before any timing: both campaign stages from the frozen
 * shape and the reviewed metrics bounds, the composer outputs and the
 * retained files. */
static int driver_plan(Driver* driver)
{
    TpRetirementCampaignShape shape = {driver->group_count, 0, driver->row_count, 0,
                                       driver->statistics.pairs_per_round, driver->untimed_count, 0, 0, 0, 0};
    for (unsigned r = 0; r < driver->row_count; ++r) shape.runtime_rows += driver->rows[r].runtime;
    for (unsigned g = 0; g < driver->group_count + driver->untimed_count; ++g)
    {
        int timed = g < driver->group_count;
        unsigned kind = timed ? driver->group_kinds[g] : driver->untimed_kinds[g - driver->group_count];
        unsigned inputs = timed ? driver->group_inputs[g] : driver->untimed_inputs[g - driver->group_count];
        uint64_t bound = driver->metrics_header + inputs * driver->metrics_input;
        if (kind == TP_RETIREMENT_GROUP_OBJECT)
        {
            if (timed)
            {
                ++shape.object_groups;
                shape.metrics_bytes += bound;
            }
            else
            {
                ++shape.untimed_object_groups;
                shape.untimed_metrics_bytes += bound;
            }
            if (bound > shape.metrics_artifact_max) shape.metrics_artifact_max = bound;
        }
    }
    TpRetirementCampaignCapacity capacity;
    TpRetirementCampaignStorePlan plan;
    TpRetirementComposeShape compose = {&driver->layout, driver->statistics.pairs_per_round, driver->code_count,
                                        driver->prior_count};
    uint64_t retained_bytes = 0;
    int valid = tp_retirement_campaign_capacity(&shape, &capacity);
    for (unsigned i = 0; valid && i < driver->retained.count; ++i)
    {
        uint64_t bytes = 0;
        valid = driver_file_size(driver->source_fd, driver->retained.files[i].source, &bytes);
        retained_bytes += bytes;
    }
    valid = valid && tp_retirement_compose_plan(&driver->store, &capacity, &compose, driver->retained.count,
                                                retained_bytes, TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES, 0, &plan);
    return valid;
}

/* Copy one of lane D's stream files into the planned store, as the in-unit
 * driver does when it writes into a pending store stream. */
static int driver_import(Driver* driver, DriverFile const* file)
{
    int fd = openat(driver->source_fd, file->source, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    FILE* input = fd >= 0 ? fdopen(fd, "rb") : NULL;
    if (fd >= 0 && !input) close(fd);
    TpRetirementPending pending = {0};
    int valid = input && tp_retirement_store_begin(&driver->store, file->path, TP_RETIREMENT_STORE_FILE_BYTES, &pending);
    Sha256 hash;
    sha256_init(&hash);
    uint64_t bytes = 0;
    unsigned char buffer[65536];
    while (valid)
    {
        size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (!count) break;
        valid = fwrite(buffer, 1, count, pending.stream) == count;
        sha256_add(&hash, buffer, (u64)count);
        bytes += count;
    }
    valid = valid && input && !ferror(input);
    if (input) fclose(input);
    if (valid)
    {
        char digest[65];
        sha256_finish_hex(&hash, (char8*)digest);
        valid = tp_retirement_store_publish(&driver->store, &pending, bytes, digest);
    }
    else if (pending.stream) tp_retirement_store_abort(&driver->store, &pending);
    return valid;
}

static int driver_import_all(Driver* driver)
{
    DriverList* lists[] = {&driver->transcript, &driver->samples[0], &driver->samples[1], &driver->metrics,
                           &driver->untimed, &driver->untimed_metrics, &driver->retained};
    int valid = 1;
    for (unsigned l = 0; valid && l < sizeof(lists) / sizeof(lists[0]); ++l)
        for (unsigned i = 0; valid && i < lists[l]->count; ++i) valid = driver_import(driver, lists[l]->files + i);
    return valid;
}

static int driver_read_file(char const* path, unsigned char** bytes, size_t* length)
{
    FILE* input = fopen(path, "rb");
    long size = -1;
    int valid = input && fseek(input, 0, SEEK_END) == 0 && (size = ftell(input)) > 0 && fseek(input, 0, SEEK_SET) == 0;
    *bytes = valid ? (unsigned char*)malloc((size_t)size) : NULL;
    valid = valid && *bytes && fread(*bytes, 1, (size_t)size, input) == (size_t)size;
    *length = valid ? (size_t)size : 0;
    if (input) fclose(input);
    return valid;
}

/* Open every directory, the store and the request structures. */
static int driver_open(Driver* driver)
{
    driver->source_fd = driver_directory(driver->source);
    driver->store_fd = driver_directory(driver->store_path);
    driver->evidence_fd = driver_directory(driver->evidence[0] ? driver->evidence : driver->store_path);
    driver->scratch_fd = driver_directory(driver->scratch);
    driver->authority_fd = driver->authority_path[0] ? driver_directory(driver->authority_path) : -1;
    driver->files = (TpRetirementStoredFile*)calloc(TP_RETIREMENT_STORE_FILES, sizeof(*driver->files));
    int valid = driver->source_fd >= 0 && driver->store_fd >= 0 && driver->evidence_fd >= 0 &&
                driver->scratch_fd >= 0 && (!driver->authority_path[0] || driver->authority_fd >= 0) && driver->files &&
                driver_read_file(driver->context_path, &driver->context, &driver->context_bytes) &&
                tp_retirement_store_open(&driver->store, driver->store_fd, driver->files, TP_RETIREMENT_STORE_FILES);
    for (unsigned i = 0; i < driver->prior_count; ++i)
        driver->prior[i] = (TpRetirementComposeClosure){driver->prior_storage[i].name, driver->prior_storage[i].path,
                                                        driver->prior_storage[i].bytes, driver->prior_storage[i].sha256};
    driver->layout = (TpRetirementComposeLayout){driver->rows, driver->group_kinds, driver->row_count,
                                                 driver->group_count, driver->population_rows, driver->untimed_count};
    return valid;
}

static void driver_request(Driver* driver)
{
    TpRetirementComposeRequest* request = &driver->request;
    *request = (TpRetirementComposeRequest){
        .store = &driver->store, .evidence_root = driver->evidence_fd, .scratch_root = driver->scratch_fd,
        .adapter_path = driver->adapter, .layout = &driver->layout, .statistics = &driver->statistics,
        .job = driver->job, .boot = driver->boot, .attempt = driver->attempt, .bound_at_ns = driver->bound_at_ns,
        .completed_at_ns = driver->completed_at_ns, .execution_plan_sha256 = driver->digests[0],
        .source_rows_sha256 = driver->digests[1], .result_input_plan_sha256 = driver->digests[2],
        .family_sha256 = driver->digests[3], .post_aa_binding_sha256 = driver->digests[4],
        .context_template = driver->context, .context_template_bytes = driver->context_bytes,
        .partitions = {driver->partitions[0], driver->partitions[1]},
        .partition_counts = {driver->partition_counts[0], driver->partition_counts[1]},
        .transcript_paths = driver->transcript.paths, .transcript_count = driver->transcript.count,
        .sample_paths = {driver->samples[0].paths, driver->samples[1].paths},
        .sample_counts = {driver->samples[0].count, driver->samples[1].count},
        .metrics_paths = driver->metrics.paths, .metrics_count = driver->metrics.count,
        .untimed_path = driver->untimed.count ? driver->untimed.paths[0] : NULL,
        .untimed_metrics_paths = driver->untimed_metrics.paths, .untimed_metrics_count = driver->untimed_metrics.count,
        .code = driver->code, .code_count = driver->code_count, .prior = driver->prior,
        .prior_count = driver->prior_count, .retained_paths = driver->retained.paths,
        .retained_count = driver->retained.count, .sealed_path = driver->sealed};
}

static int driver_compose(Driver* driver)
{
    driver_request(driver);
    int valid = tp_retirement_compose(&driver->request, &driver->result);
    if (valid && driver->authority_fd >= 0)
        valid = tp_retirement_store_receipt_authority(&driver->store, driver->authority_fd,
            TP_RETIREMENT_EXECUTION_RECEIPT_PATH, driver->job, driver->attempt, driver->digests[0],
            driver->result.context_sha256, &driver->authority);
    return valid;
}

static void driver_artifact(char const* name, TpRetirementComposeArtifact const* artifact, int last)
{
    printf("\"%s\":{\"bytes\":%" PRIu64 ",\"path\":\"%s\",\"sha256\":\"%s\"}%s", name, artifact->bytes, artifact->path,
           artifact->sha256, last ? "" : ",");
}

static int driver_main(char const* spec)
{
    Driver* driver = (Driver*)malloc(sizeof(Driver));
    int valid = driver != NULL;
    if (valid)
    {
        driver_init(driver);
        valid = driver_parse(driver, spec) && driver_open(driver) && driver_plan(driver) && driver_import_all(driver) &&
                driver_compose(driver);
    }
    if (valid)
    {
        TpRetirementComposeResult const* result = &driver->result;
        printf("COMPOSE_RESULT {");
        driver_artifact("bundle", &result->bundle, 0);
        driver_artifact("code", &result->code, 0);
        printf("\"context_sha256\":\"%s\",\"invocations\":%" PRIu64 ",\"members\":%u,\"raw_measurements_sha256\":\"%s\",",
               result->context_sha256, result->invocations, result->members, result->raw_measurements_sha256);
        driver_artifact("receipt", &result->receipt, 0);
        driver_artifact("replay", &result->replay, 0);
        printf("\"seal_entries\":%u,", result->seal_entries);
        driver_artifact("sealed", &result->sealed, 0);
        driver_artifact("series", &result->series, 1);
        if (driver->authority_fd >= 0)
            printf(",\"authority\":{\"authority_sha256\":\"%s\",\"identity_sha256\":\"%s\",\"receipt_sha256\":\"%s\"}",
                   driver->authority.authority_sha256, driver->authority.identity_sha256, driver->authority.receipt_sha256);
        printf("}\n");
    }
    else fprintf(stderr, "COMPOSE_RESULT refused\n");
    if (driver)
    {
        driver_close(driver);
        free(driver);
    }
    return valid ? 0 : 1;
}

/* A stub for the native fixtures only: one valid member per series member.
 * The reviewed adapter replaces it in the Python end-to-end test. */
static int stub_adapter(char const* input_path, char const* output_path)
{
    FILE* input = fopen(input_path, "rb");
    FILE* output = input ? fopen(output_path, "wb") : NULL;
    int valid = input && output;
    char line[4096];
    unsigned members = 0;
    if (valid) fputs("{\"schema\":\"buster-native-retirement-statistics-replay-v1\",\"version\":1,\"members\":[", output);
    while (valid && fgets(line, sizeof(line), input))
    {
        char name[TP_RETIREMENT_COMPOSE_MEMBER_BYTES];
        if (sscanf(line, "member=%127s", name) == 1)
            fprintf(output, "%s{\"member\":\"%s\",\"metric\":0,\"kind\":0,\"family_index\":0,\"outcome\":\"pass\","
                    "\"valid\":true}", members++ ? "," : "", name);
    }
    if (valid) fputs("]}\n", output);
    if (output && fclose(output) != 0) valid = 0;
    if (input) fclose(input);
    return valid && members ? 0 : 2;
}

/* ---------------------------------------------------------------- fixture */

static char const digest_a[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static char const digest_b[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static char const digest_c[] = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
static char const digest_e[] = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
static char self_path[DRIVER_PATH];

typedef struct Fixture
{
    char root[96];
    char source[128], store[128], scratch[128], authority[128], queue[128];
    Driver* driver;
} Fixture;

static void fixture_clean(char const* path)
{
    DIR* directory = opendir(path);
    struct dirent* entry;
    while (directory && (entry = readdir(directory)) != NULL)
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            char child[512];
            snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            struct stat info;
            if (lstat(child, &info) == 0 && S_ISDIR(info.st_mode))
            {
                fixture_clean(child);
                rmdir(child);
            }
            else unlink(child);
        }
    if (directory) closedir(directory);
}

static int fixture_write(char const* directory, char const* name, void const* bytes, size_t length)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* output = fopen(path, "wb");
    int valid = output && fwrite(bytes, 1, length, output) == length;
    if (output && fclose(output) != 0) valid = 0;
    return valid;
}

static void fixture_digest(void const* bytes, size_t length, char output[65])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, (u64)length);
    sha256_finish_hex(&hash, (char8*)output);
}

static int fixture_prior(Fixture* fixture, char const* name, char const* path, char const* body, char digest[65])
{
    Driver* driver = fixture->driver;
    DriverPrior* prior = driver->prior_storage + driver->prior_count;
    int valid = fixture_write(fixture->store, path, body, strlen(body));
    fixture_digest(body, strlen(body), prior->sha256);
    strcpy(prior->name, name);
    strcpy(prior->path, path);
    prior->bytes = strlen(body);
    if (digest) strcpy(digest, prior->sha256);
    ++driver->prior_count;
    return valid;
}

/* Lane D's streams for one A1 layout: an object batch group (census row 0),
 * a singleton link row with runtime (row 1) and one untimed object group
 * (cross-target code row 2), written with D's own encoders and cursor. */
static int fixture_streams(Fixture* fixture, uint64_t* completed_at)
{
    Driver* driver = fixture->driver;
    unsigned runtime_rows[1] = {1}, workspace[7];
    TpRetirementExecution execution;
    int valid = tp_retirement_execution_init(&execution, driver->statistics.seed, 2, runtime_rows, 1, 3, 60,
                                             workspace, 7);
    char path[512];
    snprintf(path, sizeof(path), "%s/retirement-execution-ab-0000.jsonl", fixture->source);
    FILE* transcript = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/retirement-metrics-ab-0000.txt", fixture->source);
    FILE* metrics_stream = fopen(path, "w+b");
    TpRetirementMetricsShards metrics;
    valid = valid && transcript && metrics_stream && tp_retirement_metrics_shards_init(&metrics, "ab", metrics_stream);
    uint64_t now = driver->bound_at_ns;
    TpRetirementInvocation invocation;
    while (valid && tp_retirement_execution_peek(&execution, &invocation) == TP_RETIREMENT_NEXT_READY)
    {
        TpProcessObservation observed = {1000 + invocation.sequence, 7000 + invocation.sequence, now + 1, now + 101, 1};
        TpProcess process = {0};
        process.wall_seconds = 100e-9;
        process.peak_rss_bytes = 4096;
        TpRetirementMetricsArtifact artifact;
        int object = !invocation.kind && invocation.group == 0;
        if (object)
        {
            char body[96];
            int length = snprintf(body, sizeof(body), "CC_METRICS fixture sequence=%" PRIu64 "\n", invocation.sequence);
            valid = tp_retirement_metrics_shards_append(&metrics, (unsigned char const*)body, (uint64_t)length, &artifact);
        }
        TpRetirementOutput output = {digest_a, digest_b, digest_c, object ? &artifact : NULL, 0};
        char line[TP_RETIREMENT_EXECUTION_LINE_CAP];
        size_t count = valid ? tp_retirement_execution_record(line, sizeof(line), &invocation, &observed, &process,
                                                              &output, driver->job, driver->attempt, driver->boot, 2) : 0;
        valid = count && fwrite(line, 1, count, transcript) == count && tp_retirement_execution_commit(&execution, 1);
        now += 200;
    }
    TpRetirementShardFile last;
    valid = valid && tp_retirement_execution_complete(&execution) && tp_retirement_metrics_shards_finish(&metrics, &last);
    if (transcript && fclose(transcript) != 0) valid = 0;
    if (metrics_stream && fclose(metrics_stream) != 0) valid = 0;
    *completed_at = now + 1000;
    /* Row samples: row 0 (object member), then row 1 (singleton, runtime). */
    snprintf(path, sizeof(path), "%s/retirement-samples-0000.jsonl", fixture->source);
    FILE* rows = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/retirement-batches-0000.jsonl", fixture->source);
    FILE* batches = fopen(path, "wb");
    valid = valid && rows && batches;
    for (unsigned unit = 0; valid && unit < 2; ++unit)
        for (unsigned round = 0; valid && round < 2; ++round)
            for (unsigned pair = 0; valid && pair < 60; ++pair)
            {
                uint64_t values[TP_RETIREMENT_SAMPLE_VALUES] = {1000 + pair, 1001 + pair + round, 4096, 4100 + unit,
                    unit ? 2000 + pair : 0, unit ? 2002 + pair : 0, 0};
                char line[TP_RETIREMENT_SAMPLE_LINE_CAP];
                size_t count = tp_retirement_sample_record(line, sizeof(line), unit, round, pair,
                                                           unit ? TP_RETIREMENT_SAMPLE_RUNTIME : 0, values);
                valid = count && fwrite(line, 1, count, rows) == count;
            }
    for (unsigned round = 0; valid && round < 2; ++round)
        for (unsigned pair = 0; valid && pair < 60; ++pair)
        {
            uint64_t values[TP_RETIREMENT_SAMPLE_VALUES] = {5000 + pair, 5003 + pair, 8192, 8200, 0, 0, 0};
            char line[TP_RETIREMENT_SAMPLE_LINE_CAP];
            size_t count = tp_retirement_batch_record(line, sizeof(line), 0, round, pair, values);
            valid = count && fwrite(line, 1, count, batches) == count;
        }
    if (rows && fclose(rows) != 0) valid = 0;
    if (batches && fclose(batches) != 0) valid = 0;
    /* Untimed reproduction batches for both variants, after the window. */
    snprintf(path, sizeof(path), "%s/retirement-untimed-batches.jsonl", fixture->source);
    FILE* untimed = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/retirement-metrics-untimed-0000.txt", fixture->source);
    FILE* untimed_stream = fopen(path, "w+b");
    TpRetirementMetricsShards untimed_metrics;
    valid = valid && untimed && untimed_stream &&
            tp_retirement_metrics_shards_init(&untimed_metrics, TP_RETIREMENT_UNTIMED_METRICS_TAG, untimed_stream);
    for (unsigned variant = 0; valid && variant < 2; ++variant)
    {
        TpRetirementUntimedBatch batch = {0};
        batch.group = 0;
        batch.variant = variant;
        batch.purpose = TP_RETIREMENT_UNTIMED_REPRODUCTION;
        batch.group_kind = TP_RETIREMENT_GROUP_OBJECT;
        TpProcessObservation observed = {90000 + variant, 99000 + variant, *completed_at + 10 + variant * 100,
                                         *completed_at + 60 + variant * 100, 1};
        char body[64];
        int length = snprintf(body, sizeof(body), "CC_METRICS untimed variant=%u\n", variant);
        TpRetirementMetricsArtifact artifact;
        char line[TP_RETIREMENT_UNTIMED_LINE_CAP];
        valid = tp_retirement_metrics_shards_append(&untimed_metrics, (unsigned char const*)body, (uint64_t)length, &artifact);
        size_t count = valid ? tp_retirement_untimed_record(line, sizeof(line), &batch, &observed, digest_a, digest_b,
                                                            digest_c, &artifact, driver->job, driver->attempt,
                                                            driver->boot) : 0;
        valid = count && fwrite(line, 1, count, untimed) == count;
    }
    valid = valid && tp_retirement_metrics_shards_finish(&untimed_metrics, &last);
    if (untimed && fclose(untimed) != 0) valid = 0;
    if (untimed_stream && fclose(untimed_stream) != 0) valid = 0;
    return valid;
}

static void fixture_row(Driver* driver, unsigned id, unsigned group, unsigned runtime, char const* stage)
{
    unsigned index = driver->row_count++;
    char const* values[TP_RETIREMENT_COMPOSE_DIMENSIONS] = {"x86_64-unknown-linux-gnu", "baseline", "none",
                                                           "direct-ssa", "0", stage};
    TpRetirementComposeRow* row = driver->rows + index;
    row->id = id;
    row->group = group;
    row->runtime = runtime;
    for (unsigned d = 0; d < TP_RETIREMENT_COMPOSE_DIMENSIONS; ++d)
    {
        strcpy(driver->dimensions[index][d], values[d]);
        row->dimensions[d] = driver->dimensions[index][d];
    }
}

static void fixture_code(Driver* driver, unsigned row, uint64_t baseline, uint64_t candidate)
{
    TpRetirementComposeCode* code = driver->code + driver->code_count++;
    code->row = row;
    for (unsigned side = 0; side < 2; ++side)
    {
        char artifact[65];
        char label[32];
        snprintf(label, sizeof(label), "artifact-%u-%u", row, side);
        fixture_digest(label, strlen(label), artifact);
        strcpy(code->sides[side].artifact_sha256, artifact);
        strcpy(code->sides[side].reproduction_sha256, artifact);
        strcpy(code->sides[side].code_sha256, digest_e);
        code->sides[side].code_bytes = side ? candidate : baseline;
    }
}

/* A complete driver over fresh private directories; the caller may mutate
 * the request before driver_open/plan/import/compose. */
static int fixture_start(Fixture* fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    strcpy(fixture->root, "/tmp/buster-retirement-compose-XXXXXX");
    int valid = mkdtemp(fixture->root) != NULL;
    char const* names[] = {"source", "store", "scratch", "authority", "queue"};
    char* paths[] = {fixture->source, fixture->store, fixture->scratch, fixture->authority, fixture->queue};
    for (unsigned i = 0; valid && i < 5; ++i)
    {
        snprintf(paths[i], 128, "%s/%s", fixture->root, names[i]);
        valid = mkdir(paths[i], 0700) == 0;
    }
    fixture->driver = valid ? (Driver*)malloc(sizeof(Driver)) : NULL;
    valid = valid && fixture->driver;
    Driver* driver = fixture->driver;
    if (valid)
    {
        driver_init(driver);
        strcpy(driver->source, fixture->source);
        strcpy(driver->store_path, fixture->store);
        strcpy(driver->scratch, fixture->scratch);
        strcpy(driver->adapter, self_path);
        strcpy(driver->authority_path, fixture->authority);
        snprintf(driver->context_path, sizeof(driver->context_path), "%s/context.json", fixture->source);
        strcpy(driver->job, "job-7");
        strcpy(driver->boot, "boot-a");
        strcpy(driver->sealed, "retirement-sealed-result.json");
        driver->attempt = 3;
        driver->bound_at_ns = 1000;
        driver->population_rows = 3;
        driver->metrics_header = 4096;
        driver->metrics_input = 16384;
        driver->group_kinds[0] = TP_RETIREMENT_GROUP_OBJECT;
        driver->group_inputs[0] = 1;
        driver->group_kinds[1] = TP_RETIREMENT_GROUP_SINGLETON;
        driver->group_inputs[1] = 1;
        driver->group_count = 2;
        driver->untimed_kinds[0] = TP_RETIREMENT_GROUP_OBJECT;
        driver->untimed_inputs[0] = 1;
        driver->untimed_count = 1;
        fixture_row(driver, 0, 0, 0, "object");
        fixture_row(driver, 1, 1, 1, "link");
        fixture_code(driver, 0, 100, 101);
        fixture_code(driver, 1, 200, 200);
        fixture_code(driver, 2, 300, 299);
        driver->layout = (TpRetirementComposeLayout){driver->rows, driver->group_kinds, driver->row_count,
                                                     driver->group_count, driver->population_rows, driver->untimed_count};
        TpRetirementComposeShape shape = {&driver->layout, 60, driver->code_count, 4};
        TpRetirementComposeBounds bounds;
        valid = tp_retirement_compose_bounds(&shape, &bounds);
        driver->statistics = (TpRetirementPlan){.seed = 20260929, .version = TP_RETIREMENT_STATISTICS_VERSION,
            .bootstrap_members_per_scope = bounds.bootstrap_members, .cell_members_per_scope = bounds.cell_members,
            .pairs_per_round = 60, .resamples = TP_RETIREMENT_MIN_RESAMPLES, .frozen_before_samples = 1};
    }
    uint64_t completed = 0;
    valid = valid && fixture_streams(fixture, &completed);
    if (valid)
    {
        driver->completed_at_ns = completed;
        strcpy(driver->digests[1], digest_a);
        strcpy(driver->digests[3], digest_b);
        valid = fixture_prior(fixture, "contract.source", "contract.md", "contract\n", NULL) &&
                fixture_prior(fixture, "workflow.execution_plan", "plan.json", "{\"plan\":1}\n", driver->digests[0]) &&
                fixture_prior(fixture, "workflow.records.result_input_plan", "result-plan.json", "{\"input\":1}\n",
                              driver->digests[2]) &&
                fixture_prior(fixture, "workflow.phases.post_aa_binding", "post.json", "{\"post\":1}\n", driver->digests[4]);
        char context[256];
        int length = snprintf(context, sizeof(context),
            "{\"post_aa_binding_sha256\":\"%s\",\"raw_measurements_sha256\":\"%064d\"}", driver->digests[4], 0);
        valid = valid && fixture_write(fixture->source, "context.json", context, (size_t)length);
        TpRetirementComposePartition* rows = &driver->partitions[0][0];
        TpRetirementComposePartition* batches = &driver->partitions[1][0];
        strcpy(rows->identity, "rows-0");
        strcpy(rows->path, "retirement-rows-manifest.json");
        rows->records = 240;
        strcpy(batches->identity, "batches-0");
        strcpy(batches->path, "retirement-batches-manifest.json");
        batches->records = 120;
        driver->partition_counts[0] = driver->partition_counts[1] = 1;
        valid = valid && driver_list_add(&driver->transcript, "retirement-execution-ab-0000.jsonl", NULL) &&
                driver_list_add(&driver->samples[0], "retirement-samples-0000.jsonl", NULL) &&
                driver_list_add(&driver->samples[1], "retirement-batches-0000.jsonl", NULL) &&
                driver_list_add(&driver->metrics, "retirement-metrics-ab-0000.txt", NULL) &&
                driver_list_add(&driver->untimed, "retirement-untimed-batches.jsonl", NULL) &&
                driver_list_add(&driver->untimed_metrics, "retirement-metrics-untimed-0000.txt", NULL);
    }
    tp_retirement_store_test_fail_sync = 0;
    tp_retirement_store_test_sync_calls = 0;
    return valid;
}

static void fixture_stop(Fixture* fixture)
{
    if (fixture->driver)
    {
        driver_close(fixture->driver);
        free(fixture->driver);
        fixture->driver = NULL;
    }
    if (fixture->root[0])
    {
        fixture_clean(fixture->root);
        CHECK(rmdir(fixture->root) == 0);
    }
    tp_retirement_store_test_fail_sync = 0;
}

static int fixture_ready(Fixture* fixture)
{
    int valid = fixture_start(fixture) && driver_open(fixture->driver) && driver_plan(fixture->driver) &&
                driver_import_all(fixture->driver);
    return valid;
}

/* ------------------------------------------------------------------ tests */

static int file_contains(int root, char const* path, char const* needle)
{
    enum { FILE_CONTAINS_BYTES = 1 << 22 };
    int fd = openat(root, path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    char* buffer = (char*)malloc(FILE_CONTAINS_BYTES);
    size_t used = 0;
    ssize_t count = 1;
    while (fd >= 0 && buffer && count > 0 && used < FILE_CONTAINS_BYTES - 1)
    {
        count = read(fd, buffer + used, FILE_CONTAINS_BYTES - 1 - used);
        if (count > 0) used += (size_t)count;
    }
    if (fd >= 0) close(fd);
    if (buffer) buffer[used] = 0;
    int found = buffer && used && strstr(buffer, needle) != NULL;
    free(buffer);
    return found;
}

static void test_compose_success(void)
{
    Fixture fixture;
    CHECK(fixture_ready(&fixture));
    Driver* driver = fixture.driver;
    CHECK(driver_compose(driver));
    TpRetirementComposeResult const* result = &driver->result;
    /* (G + U) * 2 * (warmups + rounds * pairs) = 3 * 2 * 122. */
    CHECK(result->invocations == 732);
    CHECK(result->seal_entries == 4 + 5 + 1 + 1 + 1 + 1 + 2 + 2);
    CHECK(tp_retirement_store_validate(&driver->store));
    CHECK(driver->store.count == driver->store.planned_files);
    CHECK(file_contains(driver->store_fd, TP_RETIREMENT_EXECUTION_RECEIPT_PATH, result->context_sha256));
    CHECK(file_contains(driver->store_fd, TP_RETIREMENT_COMPOSE_BUNDLE_PATH, result->raw_measurements_sha256));
    CHECK(file_contains(driver->store_fd, TP_RETIREMENT_COMPOSE_BUNDLE_PATH, "\"untimed_batches\":{\"bytes\":"));
    CHECK(file_contains(driver->store_fd, "retirement-sealed-result.json", "\"name\":\"untimed.metrics_shard.0\""));
    CHECK(file_contains(driver->store_fd, "retirement-sealed-result.json", "\"name\":\"result_input.shard.batches-0000\""));
    CHECK(file_contains(driver->store_fd, "retirement-rows-manifest.json", "\"identity\":\"samples-0000\""));
    CHECK(file_contains(driver->store_fd, TP_RETIREMENT_COMPOSE_SERIES_PATH, "member=generated_runtime/cell/row=1 metric=2 kind=1"));
    /* Code summary: rows 0..2 with 101/100, 200/200 and 299/300. */
    CHECK(file_contains(driver->store_fd, TP_RETIREMENT_COMPOSE_BUNDLE_PATH,
                        "\"aggregate_pass\":true,\"aggregate_ratio\":1.0,\"per_cell_max_ratio\":1.01,\"per_cell_pass\":true"));
    /* The producer's private authority and the scratch copy is removed. */
    CHECK(driver->authority.authority_sha256[0] != 0);
    struct stat info;
    CHECK(fstatat(driver->scratch_fd, TP_RETIREMENT_COMPOSE_SERIES_PATH, &info, 0) != 0);
    /* A second composition cannot overwrite the sealed outputs. */
    TpRetirementComposeResult again;
    CHECK(!tp_retirement_compose(&driver->request, &again));
    fixture_stop(&fixture);
}

typedef enum Refusal
{
    REFUSE_MISSING_SHARD, REFUSE_EXTRA_FILE, REFUSE_EXTRA_METRICS, REFUSE_PRIOR_DIGEST, REFUSE_CONTEXT,
    REFUSE_JOB, REFUSE_PLAN_DIGEST, REFUSE_FAMILY_COUNT, REFUSE_UNPLANNED, REFUSE_PARTITION, REFUSE_TRANSCRIPT_ORDER,
    REFUSE_SAMPLE_ORDER, REFUSE_UNTIMED_MISSING, REFUSE_ADAPTER, REFUSE_WINDOW, REFUSE_COUNT
} Refusal;

/* Swap two lines of a source stream file before import. */
static int fixture_swap_lines(Fixture* fixture, char const* name, unsigned first, unsigned second)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", fixture->source, name);
    unsigned char* bytes = NULL;
    size_t length = 0;
    int valid = driver_read_file(path, &bytes, &length);
    size_t starts[4096];
    unsigned lines = 0;
    for (size_t i = 0; valid && i < length && lines < 4096; ++i)
        if (!i || bytes[i - 1] == '\n') starts[lines++] = i;
    valid = valid && first < second && second < lines;
    unsigned char* copy = valid ? (unsigned char*)malloc(length) : NULL;
    valid = valid && copy;
    size_t used = 0;
    for (unsigned line = 0; valid && line < lines; ++line)
    {
        unsigned from = line == first ? second : line == second ? first : line;
        size_t end = from + 1 < lines ? starts[from + 1] : length;
        memcpy(copy + used, bytes + starts[from], end - starts[from]);
        used += end - starts[from];
    }
    valid = valid && used == length && fixture_write(fixture->source, name, copy, length);
    free(copy);
    free(bytes);
    return valid;
}

static void test_compose_refusal(Refusal refusal)
{
    Fixture fixture;
    int started = fixture_start(&fixture);
    CHECK(started);
    Driver* driver = fixture.driver;
    int ready = started;
    if (ready && refusal == REFUSE_EXTRA_METRICS)
        ready = fixture_write(fixture.source, "retirement-metrics-ab-0001.txt", "orphan\n", 7) &&
                driver_list_add(&driver->metrics, "retirement-metrics-ab-0001.txt", NULL);
    if (ready && refusal == REFUSE_EXTRA_FILE)
        ready = fixture_write(fixture.source, "retirement-extra.log", "log\n", 4) &&
                driver_list_add(&driver->retained, "retirement-extra.log", NULL);
    if (ready && refusal == REFUSE_TRANSCRIPT_ORDER)
        ready = fixture_swap_lines(&fixture, "retirement-execution-ab-0000.jsonl", 20, 21);
    if (ready && refusal == REFUSE_SAMPLE_ORDER)
        ready = fixture_swap_lines(&fixture, "retirement-samples-0000.jsonl", 3, 4);
    if (ready && refusal == REFUSE_UNTIMED_MISSING)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/retirement-untimed-batches.jsonl", fixture.source);
        unsigned char* bytes = NULL;
        size_t length = 0;
        ready = driver_read_file(path, &bytes, &length);
        unsigned char* newline = ready ? (unsigned char*)memchr(bytes, '\n', length) : NULL;
        ready = newline && fixture_write(fixture.source, "retirement-untimed-batches.jsonl", bytes,
                                         (size_t)(newline - bytes) + 1);
        free(bytes);
    }
    if (ready && refusal == REFUSE_WINDOW) driver->completed_at_ns = driver->bound_at_ns + 5000;
    ready = ready && driver_open(driver);
    if (ready && refusal != REFUSE_UNPLANNED) ready = driver_plan(driver);
    ready = ready && driver_import_all(driver);
    CHECK(ready);
    if (ready)
    {
        switch (refusal)
        {
        case REFUSE_MISSING_SHARD:
            CHECK(driver_list_add(&driver->samples[1], "retirement-batches-0001.jsonl", NULL));
            break;
        case REFUSE_EXTRA_FILE:
            CHECK(driver_list_remove(&driver->retained, "retirement-extra.log"));
            break;
        case REFUSE_PRIOR_DIGEST:
            driver->prior_storage[0].sha256[0] = driver->prior_storage[0].sha256[0] == 'f' ? 'e' : 'f';
            break;
        case REFUSE_CONTEXT:
            driver->context[driver->context_bytes - 3] = '1';
            break;
        case REFUSE_JOB:
            strcpy(driver->job, "job-8");
            break;
        case REFUSE_PLAN_DIGEST:
            driver->digests[0][0] = driver->digests[0][0] == 'f' ? 'e' : 'f';
            break;
        case REFUSE_FAMILY_COUNT:
            driver->statistics.bootstrap_members_per_scope += 1;
            break;
        case REFUSE_PARTITION:
            driver->partitions[0][0].records = 239;
            break;
        case REFUSE_ADAPTER:
            strcpy(driver->adapter, "/nonexistent/retirement-replay");
            break;
        default:
            break;
        }
        static char const* const stages[REFUSE_COUNT] = {"inventory", "inventory", "transcript", "prior", "context",
            "transcript", "prior", "bounds", "request", "partitions", "transcript", "samples", "untimed", "adapter",
            "transcript"};
        CHECK(!driver_compose(driver));
        CHECK(driver->result.refused && !strcmp(driver->result.refused, stages[refusal]));
        if (driver->result.refused && strcmp(driver->result.refused, stages[refusal]))
            fprintf(stderr, "COMPOSE_TEST refusal=%u stage=%s\n", (unsigned)refusal, driver->result.refused);
        /* The refused attempt is poisoned and published nothing new. */
        CHECK(driver->store.failed);
        struct stat info;
        CHECK(fstatat(driver->store_fd, "retirement-sealed-result.json", &info, AT_SYMLINK_NOFOLLOW) != 0);
    }
    fixture_stop(&fixture);
}

static void test_compose_refusals(void)
{
    for (unsigned refusal = 0; refusal < REFUSE_COUNT; ++refusal) test_compose_refusal((Refusal)refusal);
}

static void test_budget_and_settle(void)
{
    /* A family whose single adapter input exceeds the per-file store cap is
     * refused before any timing. */
    static TpRetirementComposeRow rows[4000];
    static unsigned kinds[4000];
    char const* values[TP_RETIREMENT_COMPOSE_DIMENSIONS] = {"x86_64-unknown-linux-gnu", "baseline", "none",
                                                           "direct-ssa", "0", "link"};
    for (unsigned i = 0; i < 4000; ++i)
    {
        rows[i] = (TpRetirementComposeRow){i, i, i == 0, {values[0], values[1], values[2], values[3], values[4], values[5]}};
        kinds[i] = TP_RETIREMENT_GROUP_SINGLETON;
    }
    kinds[1] = TP_RETIREMENT_GROUP_OBJECT;
    rows[1].dimensions[5] = "object";
    TpRetirementComposeLayout large = {rows, kinds, 4000, 4000, 4000, 0};
    TpRetirementComposeShape shape = {&large, 254, 1, 1};
    TpRetirementComposeBounds bounds;
    CHECK(!tp_retirement_compose_bounds(&shape, &bounds));
    shape.pairs = 60;
    TpRetirementComposeLayout small = {rows, kinds, 2, 2, 4000, 0};
    shape.layout = &small;
    CHECK(tp_retirement_compose_bounds(&shape, &bounds));
    shape.pairs = 61;
    CHECK(!tp_retirement_compose_bounds(&shape, &bounds));
    /* A layout with no runtime row has no generated-runtime cells. */
    rows[0].runtime = 0;
    shape.pairs = 60;
    CHECK(!tp_retirement_compose_bounds(&shape, &bounds));
    rows[0].runtime = 1;

    Fixture fixture;
    CHECK(fixture_start(&fixture) && driver_open(fixture.driver));
    Driver* driver = fixture.driver;
    /* The plan refuses a retained-file reservation beyond the store. */
    TpRetirementCampaignShape campaign = {2, 1, 2, 1, 60, 1, 1, 4096 + 16384, 4096 + 16384, 4096 + 16384};
    TpRetirementCampaignCapacity capacity;
    TpRetirementCampaignStorePlan plan;
    TpRetirementComposeShape compose = {&driver->layout, 60, driver->code_count, driver->prior_count};
    CHECK(tp_retirement_campaign_capacity(&campaign, &capacity));
    CHECK(!tp_retirement_compose_plan(&driver->store, &capacity, &compose, TP_RETIREMENT_STORE_FILES, 0, 3, 0, &plan));
    tp_retirement_store_close(&driver->store);
    CHECK(tp_retirement_store_open(&driver->store, driver->store_fd, driver->files, TP_RETIREMENT_STORE_FILES));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, 0, 0, 3, 0, &plan));
    /* Settle only lowers the reservation, never below the published count. */
    unsigned planned = driver->store.planned_files;
    CHECK(!tp_retirement_store_settle(&driver->store, planned + 1));
    tp_retirement_store_close(&driver->store);
    CHECK(tp_retirement_store_open(&driver->store, driver->store_fd, driver->files, TP_RETIREMENT_STORE_FILES));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, 0, 0, 3, 0, &plan));
    CHECK(driver_import(driver, driver->transcript.files) && driver_import(driver, driver->samples[0].files));
    CHECK(!tp_retirement_store_settle(&driver->store, 1));
    tp_retirement_store_close(&driver->store);
    CHECK(tp_retirement_store_open(&driver->store, driver->store_fd, driver->files, TP_RETIREMENT_STORE_FILES));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, 0, 0, 3, 0, &plan));
    CHECK(driver_import(driver, driver->metrics.files));
    CHECK(!tp_retirement_store_validate(&driver->store));
    tp_retirement_store_close(&driver->store);
    CHECK(tp_retirement_store_open(&driver->store, driver->store_fd, driver->files, TP_RETIREMENT_STORE_FILES));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, 0, 0, 3, 0, &plan));
    CHECK(driver_import(driver, driver->untimed.files));
    CHECK(tp_retirement_store_settle(&driver->store, 1));
    CHECK(tp_retirement_store_validate(&driver->store));
    fixture_stop(&fixture);
}

static int handoff_ready(Fixture* fixture)
{
    int valid = fixture_ready(fixture) && driver_compose(fixture->driver);
    return valid;
}

static void test_handoff(void)
{
    Fixture fixture;
    CHECK(handoff_ready(&fixture));
    Driver* driver = fixture.driver;
    int queue = driver_directory(fixture.queue);
    TpRetirementAuthorityJournal journal;
    char const* context = driver->result.context_sha256;
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0], context,
                                              &driver->authority) == TP_RETIREMENT_AUTHORITY_ABSENT);
    /* The authenticated handoff's identities must be the authority's. */
    CHECK(!tp_retirement_store_authority_handoff(driver->store_fd, driver->authority_fd, queue, 8, 3,
                                                 driver->digests[0], context, &driver->authority, &journal));
    CHECK(!tp_retirement_store_authority_handoff(driver->store_fd, driver->authority_fd, queue, 7, 4,
                                                 driver->digests[0], context, &driver->authority, &journal));
    /* The pre-sample context is never the final authority context. */
    CHECK(!tp_retirement_store_authority_handoff(driver->store_fd, driver->authority_fd, queue, 7, 3,
                                                 driver->digests[0], driver->result.raw_measurements_sha256,
                                                 &driver->authority, &journal));
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0], context,
                                              &driver->authority) == TP_RETIREMENT_AUTHORITY_ABSENT);
    CHECK(tp_retirement_store_authority_handoff(driver->store_fd, driver->authority_fd, queue, 7, 3,
                                                driver->digests[0], context, &driver->authority, &journal));
    CHECK(!strcmp(journal.path, "authority-job-7-3.journal") && journal.bytes > 0);
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0], context,
                                              &driver->authority) == TP_RETIREMENT_AUTHORITY_COMPLETE);
    /* A retry never overwrites the sealed copy or journal. */
    struct stat before, after;
    CHECK(fstatat(queue, journal.path, &before, AT_SYMLINK_NOFOLLOW) == 0);
    CHECK(!tp_retirement_store_authority_handoff(driver->store_fd, driver->authority_fd, queue, 7, 3,
                                                 driver->digests[0], context, &driver->authority, &journal));
    CHECK(fstatat(queue, "authority-job-7-3.journal", &after, AT_SYMLINK_NOFOLLOW) == 0 &&
          after.st_ino == before.st_ino && after.st_size == before.st_size);
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0], context,
                                              &driver->authority) == TP_RETIREMENT_AUTHORITY_COMPLETE);
    /* A removed transcript shard makes the completed handoff unreadable. */
    CHECK(unlinkat(driver->store_fd, "retirement-execution-ab-0000.jsonl", 0) == 0);
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0], context,
                                              &driver->authority) == TP_RETIREMENT_AUTHORITY_INCOMPLETE);
    close(queue);
    fixture_stop(&fixture);

    /* A copy published without its journal (crash between the two) is an
     * incomplete handoff: the retry refuses and the evidence stays. */
    CHECK(handoff_ready(&fixture));
    driver = fixture.driver;
    queue = driver_directory(fixture.queue);
    context = driver->result.context_sha256;
    CHECK(tp_retirement_store_authority_copy(driver->store_fd, driver->authority_fd, queue, driver->job, 3,
                                             driver->digests[0], context, &driver->authority));
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0], context,
                                              &driver->authority) == TP_RETIREMENT_AUTHORITY_INCOMPLETE);
    CHECK(!tp_retirement_store_authority_handoff(driver->store_fd, driver->authority_fd, queue, 7, 3,
                                                 driver->digests[0], context, &driver->authority, &journal));
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0], context,
                                              &driver->authority) == TP_RETIREMENT_AUTHORITY_INCOMPLETE);
    close(queue);
    fixture_stop(&fixture);

    /* A failed journal sync leaves a pending prefix: incomplete, not ACKable. */
    CHECK(handoff_ready(&fixture));
    driver = fixture.driver;
    queue = driver_directory(fixture.queue);
    context = driver->result.context_sha256;
    tp_retirement_store_test_sync_calls = 0;
    /* authority_copy syncs: pending dir, file, link dir, unlink dir (4);
     * the journal's first sync is its pending directory entry. */
    tp_retirement_store_test_fail_sync = 5;
    CHECK(!tp_retirement_store_authority_handoff(driver->store_fd, driver->authority_fd, queue, 7, 3,
                                                 driver->digests[0], context, &driver->authority, &journal));
    tp_retirement_store_test_fail_sync = 0;
    CHECK(journal.path[0] == 0);
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0], context,
                                              &driver->authority) == TP_RETIREMENT_AUTHORITY_INCOMPLETE);
    close(queue);
    fixture_stop(&fixture);

    /* No producer authority: nothing is copied or journalled. */
    CHECK(fixture_start(&fixture));
    driver = fixture.driver;
    driver->authority_path[0] = 0;
    CHECK(driver_open(driver) && driver_plan(driver) && driver_import_all(driver) && driver_compose(driver));
    int authority = driver_directory(fixture.authority);
    queue = driver_directory(fixture.queue);
    TpRetirementReceiptAuthority forged = {0};
    strcpy(forged.job, "job-7");
    forged.attempt = 3;
    strcpy(forged.plan_sha256, driver->digests[0]);
    strcpy(forged.context_sha256, driver->result.context_sha256);
    strcpy(forged.receipt_sha256, driver->result.receipt.sha256);
    strcpy(forged.identity_sha256, digest_a);
    strcpy(forged.authority_sha256, digest_b);
    CHECK(!tp_retirement_store_authority_handoff(driver->store_fd, authority, queue, 7, 3, driver->digests[0],
                                                 driver->result.context_sha256, &forged, &journal));
    CHECK(tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3, driver->digests[0],
        driver->result.context_sha256, &forged) == TP_RETIREMENT_AUTHORITY_ABSENT);
    close(authority);
    close(queue);
    fixture_stop(&fixture);
}

int main(int argc, char** argv)
{
    int result = 0;
    if (argc == 3 && !strcmp(argv[1], "compose")) result = driver_main(argv[2]);
    else if (argc == 6 && !strcmp(argv[1], "retirement-replay") && !strcmp(argv[2], "--input") &&
             !strcmp(argv[4], "--output"))
        result = stub_adapter(argv[3], argv[5]);
    else
    {
        ssize_t length = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
        CHECK(length > 0 && (size_t)length < sizeof(self_path) - 1);
        if (length > 0) self_path[length] = 0;
        test_compose_success();
        test_compose_refusals();
        test_budget_and_settle();
        test_handoff();
        fprintf(stderr, "COMPOSE_TEST assertions=%u failures=%u\n", assertions, failures);
        result = failures ? 1 : 0;
    }
    return result;
}
#endif
