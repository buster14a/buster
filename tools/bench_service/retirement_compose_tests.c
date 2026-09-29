/* #881-E composer fixtures and command-line driver (Linux only).
 *
 * Link with retirement_compose.c, retirement_compose_json.c,
 * retirement_result.c (built with BUSTER_RETIREMENT_STORE_TEST) and
 * tools/throughput/shared.c. Modes:
 *   (no arguments)                 native fail-closed fixtures, printed as
 *                                  `COMPOSE_TEST assertions=N failures=N`
 *   compose SPEC                   plan a fresh store with the declared
 *                                  retained files, import lane D's stream
 *                                  files, compose, optionally issue the
 *                                  producer authority, print COMPOSE_RESULT
 *   canonical FILE                 print a JSON document's canonical bytes
 *   context BINDING RAW            print _execution_context(binding, RAW)
 *   retirement-replay --input I --output O
 *                                  a stub adapter for the native fixtures only
 *                                  (a `stub-mode` file in its directory selects
 *                                  a slow or malformed run)
 * The Python end-to-end test (retirement_compose_test.py) drives these with
 * the reviewed adapter and runs the binding validator on the output.
 * No fixture here is service admission or performance evidence.
 *
 * Map: Driver, driver_parse, driver_plan, driver_import_all, driver_compose,
 * driver_main, stub_adapter, fixture_streams, fixture_start, fixture_clean,
 * test_canonical_json, test_compose_success, test_compose_series_shards,
 * test_compose_refusals, test_production_capacity, test_budget_and_settle,
 * test_handoff.
 */
#define _GNU_SOURCE 1
#include "retirement_compose.h"
#ifdef __linux__
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern unsigned tp_retirement_store_test_sync_calls, tp_retirement_store_test_fail_sync;
extern uint64_t tp_retirement_compose_test_shard_bytes;
BUSTER_GLOBAL_LOCAL unsigned assertions, failures;
#define CHECK(value) do { ++assertions; if (!(value)) { ++failures; fprintf(stderr, "COMPOSE_TEST line=%d: %s\n", __LINE__, #value); } } while (0)

#define DRIVER_ROWS 64u
#define DRIVER_GROUPS 64u
#define DRIVER_FILES 64u
#define DRIVER_PRIOR 128u
#define DRIVER_PATH 512u
#define DRIVER_LINE 4096u
#define DRIVER_WORDS 16u
/* Every driver owns one arena for its tables and file copies. */
#define DRIVER_ARENA_BYTES (UINT64_C(1) << 30)
#define DRIVER_READ_BYTES (UINT64_C(64) << 20)
/* The store root also holds the binding and three reserved controls. */
#define DRIVER_EXTERNAL_ENTRIES (TP_RETIREMENT_CAMPAIGN_MIN_EXTERNAL_STORE_ENTRIES + 1u)
/* The stub adapter's slow mode outlives the fixture's adapter limit. */
#define STUB_SLOW_SECONDS 30
#define FIXTURE_ADAPTER_TIMEOUT_NS UINT64_C(300000000)
#define FIXTURE_PAIRS 60u
#define FIXTURE_DIRECTORY_DEPTH 16u

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

typedef struct DriverRetained
{
    char kind[TP_RETIREMENT_COMPOSE_KIND_BYTES + 1];
    char prefix[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char suffix[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    int group;
} DriverRetained;

typedef struct Driver
{
    Arena* arena;
    char source[DRIVER_PATH], store_path[DRIVER_PATH], scratch[DRIVER_PATH];
    char adapter[DRIVER_PATH], adapter_sha256[65], authority_path[DRIVER_PATH];
    char binding[TP_RETIREMENT_STORE_PATH_BYTES + 1], binding_sha256[65];
    char job[TP_RETIREMENT_STORE_TOKEN_CAPACITY], boot[TP_RETIREMENT_STORE_TOKEN_CAPACITY];
    char sealed[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    char digests[5][65];
    char dimensions[DRIVER_ROWS][TP_RETIREMENT_COMPOSE_DIMENSIONS][65];
    int source_fd, store_fd, scratch_fd, authority_fd;
    uint64_t attempt, bound_at_ns, completed_at_ns, metrics_header, metrics_input, timeout_ns;
    TpRetirementComposeRow rows[DRIVER_ROWS];
    unsigned group_kinds[DRIVER_GROUPS], group_inputs[DRIVER_GROUPS], group_count, row_count;
    unsigned untimed_kinds[DRIVER_GROUPS], untimed_inputs[DRIVER_GROUPS], untimed_count, population_rows;
    TpRetirementComposeCode code[DRIVER_ROWS];
    unsigned code_count;
    TpRetirementComposePartition partitions[2][TP_RETIREMENT_COMPOSE_PARTITIONS];
    unsigned partition_counts[2];
    /* Sealed inputs, then retained files (exact declarations and group
     * members): every list is imported into the store. */
    DriverList transcript, samples[2], metrics, untimed, untimed_metrics, retained_files, aa_transcript;
    char aa_tag[TP_RETIREMENT_METRICS_TAG_BYTES + 1];
    DriverRetained retained_text[TP_RETIREMENT_COMPOSE_RETAINED_ENTRIES];
    TpRetirementComposeRetained retained[TP_RETIREMENT_COMPOSE_RETAINED_ENTRIES];
    unsigned retained_count;
    DriverPrior prior_storage[DRIVER_PRIOR];
    TpRetirementComposeClosure prior[DRIVER_PRIOR];
    unsigned prior_count;
    TpRetirementComposeDeclaration declaration;
    TpRetirementPlan statistics;
    TpRetirementComposeLayout layout;
    TpRetirementStore store;
    TpRetirementStoredFile* files;
    TpRetirementComposeRequest request;
    TpRetirementComposeResult result;
    TpRetirementReceiptAuthority authority;
} Driver;

BUSTER_GLOBAL_LOCAL Driver* driver_create(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = DRIVER_ARENA_BYTES, .flags = {.no_pool = 1}});
    Driver* driver = arena ? (Driver*)tp_retirement_compose_allocate(arena, sizeof(Driver)) : NULL;
    if (driver)
    {
        driver->arena = arena;
        driver->source_fd = driver->store_fd = driver->scratch_fd = driver->authority_fd = -1;
        driver->store.root = -1;
    }
    else if (arena) arena_destroy(arena, 1);
    return driver;
}

BUSTER_GLOBAL_LOCAL void driver_destroy(Driver* driver)
{
    if (driver)
    {
        tp_retirement_store_close(&driver->store);
        int* descriptors[] = {&driver->source_fd, &driver->store_fd, &driver->scratch_fd, &driver->authority_fd};
        for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(descriptors); ++i)
            if (*descriptors[i] >= 0)
            {
                close(*descriptors[i]);
                *descriptors[i] = -1;
            }
        arena_destroy(driver->arena, 1);
    }
}

BUSTER_GLOBAL_LOCAL int driver_directory(char const* path)
{
    int fd = path && path[0] ? open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
    return fd;
}

BUSTER_GLOBAL_LOCAL int driver_list_add(DriverList* list, char const* path, char const* source)
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

BUSTER_GLOBAL_LOCAL int driver_list_remove(DriverList* list, char const* path)
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

BUSTER_GLOBAL_LOCAL int driver_kind(char const* text, unsigned* kind)
{
    int valid = text && (!strcmp(text, "object") || !strcmp(text, "singleton"));
    if (valid) *kind = !strcmp(text, "object") ? TP_RETIREMENT_GROUP_OBJECT : TP_RETIREMENT_GROUP_SINGLETON;
    return valid;
}

BUSTER_GLOBAL_LOCAL int driver_digest_copy(char output[65], char const* text)
{
    int valid = text && strlen(text) == 64;
    if (valid) strcpy(output, text);
    return valid;
}

BUSTER_GLOBAL_LOCAL int driver_copy(char* output, size_t capacity, char const* text)
{
    int valid = text && strlen(text) < capacity;
    if (valid) strcpy(output, text);
    return valid;
}

/* A retained declaration entry: `reserved` or an unreserved byte bound. */
BUSTER_GLOBAL_LOCAL int driver_retain(Driver* driver, char const* kind, char const* prefix, char const* suffix,
                                      unsigned files_max, char const* reservation)
{
    unsigned index = driver->retained_count;
    int valid = index < TP_RETIREMENT_COMPOSE_RETAINED_ENTRIES;
    DriverRetained* text = valid ? driver->retained_text + index : NULL;
    valid = valid && driver_copy(text->kind, sizeof(text->kind), kind) &&
            driver_copy(text->prefix, sizeof(text->prefix), prefix) &&
            (!suffix || driver_copy(text->suffix, sizeof(text->suffix), suffix));
    if (valid)
    {
        int reserved = !strcmp(reservation, "reserved");
        text->group = suffix != NULL;
        driver->retained[index] = (TpRetirementComposeRetained){text->kind, text->prefix, suffix ? text->suffix : NULL,
            files_max, (unsigned)reserved, reserved ? 0 : strtoull(reservation, NULL, 10)};
        ++driver->retained_count;
    }
    return valid;
}

/* One whitespace-separated directive per line; retirement_compose_test.py
 * writes: source store scratch adapter authority binding sealed identity
 * digests statistics population metrics-budget group untimed-group row code
 * partition transcript samples metrics untimed untimed-metrics retain-file
 * retain-declared (declared, never imported) retain-group retain-member
 * aa-transcript (a retained A/A shard) aa-metrics-tag prior timeout. */
BUSTER_GLOBAL_LOCAL int driver_parse(Driver* driver, char const* spec)
{
    FILE* input = fopen(spec, "rb");
    int valid = input != NULL;
    char line[DRIVER_LINE];
    while (valid && fgets(line, sizeof(line), input))
    {
        char* words[DRIVER_WORDS] = {0};
        unsigned count = 0;
        char* state = NULL;
        for (char* token = strtok_r(line, " \t\r\n", &state); token && count < DRIVER_WORDS;
             token = strtok_r(NULL, " \t\r\n", &state))
            words[count++] = token;
        char const* key = count ? words[0] : "";
        if (!count) valid = 1;
        else if (!strcmp(key, "source") && count == 2) valid = driver_copy(driver->source, DRIVER_PATH, words[1]);
        else if (!strcmp(key, "store") && count == 2) valid = driver_copy(driver->store_path, DRIVER_PATH, words[1]);
        else if (!strcmp(key, "scratch") && count == 2) valid = driver_copy(driver->scratch, DRIVER_PATH, words[1]);
        else if (!strcmp(key, "adapter") && count == 3)
            valid = driver_copy(driver->adapter, DRIVER_PATH, words[1]) && driver_digest_copy(driver->adapter_sha256, words[2]);
        else if (!strcmp(key, "authority") && count == 2)
            valid = driver_copy(driver->authority_path, DRIVER_PATH, words[1]);
        else if (!strcmp(key, "binding") && count == 3)
            valid = driver_copy(driver->binding, sizeof(driver->binding), words[1]) &&
                    driver_digest_copy(driver->binding_sha256, words[2]);
        else if (!strcmp(key, "sealed") && count == 2) valid = driver_copy(driver->sealed, sizeof(driver->sealed), words[1]);
        else if (!strcmp(key, "timeout") && count == 2) driver->timeout_ns = strtoull(words[1], NULL, 10);
        /* A fixture-only series shard size (#1880), so a small family spans
         * several shards; the validator test patches its size to match. */
        else if (!strcmp(key, "series-shard-bytes") && count == 2)
            tp_retirement_compose_test_shard_bytes = strtoull(words[1], NULL, 10);
        else if (!strcmp(key, "identity") && count == 6)
        {
            valid = driver_copy(driver->job, sizeof(driver->job), words[1]) &&
                    driver_copy(driver->boot, sizeof(driver->boot), words[3]);
            driver->attempt = strtoull(words[2], NULL, 10);
            driver->bound_at_ns = strtoull(words[4], NULL, 10);
            driver->completed_at_ns = strtoull(words[5], NULL, 10);
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
                valid = driver_copy(driver->dimensions[index][d], 65, words[4 + d]);
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
            valid = (population || !strcmp(words[1], "rows")) && index < TP_RETIREMENT_COMPOSE_PARTITIONS;
            TpRetirementComposePartition* partition = valid ? driver->partitions[population] + index : NULL;
            valid = valid && driver_copy(partition->identity, sizeof(partition->identity), words[2]) &&
                    driver_copy(partition->path, sizeof(partition->path), words[3]);
            if (valid)
            {
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
        else if (!strcmp(key, "retain-file") && (count == 4 || count == 5))
            valid = driver_retain(driver, words[1], words[2], NULL, 1, words[3]) &&
                    driver_list_add(&driver->retained_files, words[2], count == 5 ? words[4] : NULL);
        else if (!strcmp(key, "aa-transcript") && count == 2)
            valid = driver_list_add(&driver->aa_transcript, words[1], NULL);
        else if (!strcmp(key, "aa-metrics-tag") && count == 2)
            valid = driver_copy(driver->aa_tag, sizeof(driver->aa_tag), words[1]);
        else if (!strcmp(key, "retain-declared") && count == 4)
            valid = driver_retain(driver, words[1], words[2], NULL, 1, words[3]);
        else if (!strcmp(key, "retain-group") && count == 6)
            valid = words[3][0] == '=' && driver_retain(driver, words[1], words[2], words[3] + 1,
                                                        (unsigned)strtoul(words[4], NULL, 10), words[5]);
        else if (!strcmp(key, "retain-member") && (count == 2 || count == 3))
            valid = driver_list_add(&driver->retained_files, words[1], count == 3 ? words[2] : NULL);
        else if (!strcmp(key, "prior") && count == 5)
        {
            DriverPrior* prior = driver->prior_count < DRIVER_PRIOR ? driver->prior_storage + driver->prior_count : NULL;
            valid = prior && driver_copy(prior->name, sizeof(prior->name), words[1]) &&
                    driver_copy(prior->path, sizeof(prior->path), words[2]) && driver_digest_copy(prior->sha256, words[4]);
            if (valid)
            {
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

BUSTER_GLOBAL_LOCAL int driver_file_size(int root, char const* path, uint64_t* bytes)
{
    struct stat info = {0};
    int valid = fstatat(root, path, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
    *bytes = valid ? (uint64_t)info.st_size : 0;
    return valid;
}

/* The declaration, prior closure and layout views over the parsed text. */
BUSTER_GLOBAL_LOCAL void driver_views(Driver* driver)
{
    uint64_t prior_bytes = 0;
    for (unsigned i = 0; i < driver->prior_count; ++i)
    {
        DriverPrior const* prior = driver->prior_storage + i;
        driver->prior[i] = (TpRetirementComposeClosure){prior->name, prior->path, prior->bytes, prior->sha256};
        prior_bytes += prior->bytes;
    }
    for (unsigned i = 0; i < driver->retained_count; ++i)
    {
        DriverRetained const* text = driver->retained_text + i;
        driver->retained[i].kind = text->kind;
        driver->retained[i].prefix = text->prefix;
        driver->retained[i].suffix = text->group ? text->suffix : NULL;
    }
    driver->declaration = (TpRetirementComposeDeclaration){driver->retained, driver->retained_count,
                                                           driver->prior_count, prior_bytes};
    driver->layout = (TpRetirementComposeLayout){driver->rows, driver->group_kinds, driver->row_count,
                                                 driver->group_count, driver->population_rows, driver->untimed_count};
}

BUSTER_GLOBAL_LOCAL int driver_capacity(Driver* driver, TpRetirementCampaignCapacity* capacity)
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
    int valid = tp_retirement_campaign_capacity(&shape, capacity);
    return valid;
}

/* Reserve the store before any timing: both campaign stages from the frozen
 * shape and the reviewed metrics bounds, the composer outputs, the retained
 * declaration and, as external entries, the prior closure and binding. */
BUSTER_GLOBAL_LOCAL int driver_plan(Driver* driver)
{
    TpRetirementCampaignCapacity capacity;
    TpRetirementCampaignStorePlan plan;
    driver_views(driver);
    TpRetirementComposeShape compose = {&driver->layout, driver->statistics.pairs_per_round, driver->code_count,
                                        driver->prior_count};
    uint64_t binding = 0;
    int valid = driver_capacity(driver, &capacity) && driver_file_size(driver->store_fd, driver->binding, &binding) &&
                tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration,
                                           DRIVER_EXTERNAL_ENTRIES, binding, &plan);
    return valid;
}

/* Copy one of lane D's stream files into the planned store, as the in-unit
 * driver does when it writes into a pending store stream. */
BUSTER_GLOBAL_LOCAL int driver_import(Driver* driver, DriverFile const* file)
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
    size_t count = valid ? 1 : 0;
    while (valid && count)
    {
        count = fread(buffer, 1, sizeof(buffer), input);
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

BUSTER_GLOBAL_LOCAL int driver_import_all(Driver* driver)
{
    DriverList* lists[] = {&driver->transcript, &driver->samples[0], &driver->samples[1], &driver->metrics,
                           &driver->untimed, &driver->untimed_metrics, &driver->retained_files};
    int valid = 1;
    for (unsigned l = 0; valid && l < BUSTER_ARRAY_LENGTH(lists); ++l)
        for (unsigned i = 0; valid && i < lists[l]->count; ++i) valid = driver_import(driver, lists[l]->files + i);
    return valid;
}

/* A whole regular file into the arena (at most DRIVER_READ_BYTES). */
BUSTER_GLOBAL_LOCAL int driver_read_file(Arena* arena, char const* path, unsigned char** bytes, size_t* length)
{
    FILE* input = fopen(path, "rb");
    long size = -1;
    int valid = input && fseek(input, 0, SEEK_END) == 0 && (size = ftell(input)) > 0 &&
                (uint64_t)size <= DRIVER_READ_BYTES && fseek(input, 0, SEEK_SET) == 0;
    *bytes = valid ? (unsigned char*)tp_retirement_compose_allocate(arena, (uint64_t)size) : NULL;
    valid = valid && *bytes && fread(*bytes, 1, (size_t)size, input) == (size_t)size;
    *length = valid ? (size_t)size : 0;
    if (input) fclose(input);
    return valid;
}

/* Open every directory, the store and the request structures. */
BUSTER_GLOBAL_LOCAL int driver_open(Driver* driver)
{
    driver->source_fd = driver_directory(driver->source);
    driver->store_fd = driver_directory(driver->store_path);
    driver->scratch_fd = driver_directory(driver->scratch);
    driver->authority_fd = driver->authority_path[0] ? driver_directory(driver->authority_path) : -1;
    driver->files = (TpRetirementStoredFile*)tp_retirement_compose_allocate(driver->arena,
                        (uint64_t)TP_RETIREMENT_STORE_FILES * sizeof(TpRetirementStoredFile));
    int valid = driver->source_fd >= 0 && driver->store_fd >= 0 && driver->scratch_fd >= 0 &&
                (!driver->authority_path[0] || driver->authority_fd >= 0) && driver->files &&
                tp_retirement_store_open(&driver->store, driver->store_fd, driver->files, TP_RETIREMENT_STORE_FILES);
    driver_views(driver);
    return valid;
}

BUSTER_GLOBAL_LOCAL void driver_request(Driver* driver)
{
    TpRetirementComposeRequest* request = &driver->request;
    *request = (TpRetirementComposeRequest){
        .store = &driver->store, .scratch_root = driver->scratch_fd, .adapter_path = driver->adapter,
        .adapter_sha256 = driver->adapter_sha256, .adapter_timeout_ns = driver->timeout_ns, .layout = &driver->layout,
        .statistics = &driver->statistics, .declaration = &driver->declaration, .job = driver->job,
        .boot = driver->boot, .attempt = driver->attempt, .bound_at_ns = driver->bound_at_ns,
        .completed_at_ns = driver->completed_at_ns, .execution_plan_sha256 = driver->digests[0],
        .source_rows_sha256 = driver->digests[1], .result_input_plan_sha256 = driver->digests[2],
        .family_sha256 = driver->digests[3], .post_aa_binding_sha256 = driver->digests[4],
        .binding_path = driver->binding, .binding_sha256 = driver->binding_sha256,
        .partitions = {driver->partitions[0], driver->partitions[1]},
        .partition_counts = {driver->partition_counts[0], driver->partition_counts[1]},
        .transcript_paths = driver->transcript.paths, .transcript_count = driver->transcript.count,
        .sample_paths = {driver->samples[0].paths, driver->samples[1].paths},
        .sample_counts = {driver->samples[0].count, driver->samples[1].count},
        .metrics_paths = driver->metrics.paths, .metrics_count = driver->metrics.count,
        .untimed_path = driver->untimed.count ? driver->untimed.paths[0] : NULL,
        .untimed_metrics_paths = driver->untimed_metrics.paths, .untimed_metrics_count = driver->untimed_metrics.count,
        .aa_transcript_paths = driver->aa_transcript.paths, .aa_transcript_count = driver->aa_transcript.count,
        .aa_metrics_tag = driver->aa_tag[0] ? driver->aa_tag : NULL,
        .code = driver->code, .code_count = driver->code_count, .prior = driver->prior,
        .prior_count = driver->prior_count, .sealed_path = driver->sealed};
}

BUSTER_GLOBAL_LOCAL int driver_compose(Driver* driver)
{
    driver_request(driver);
    int valid = tp_retirement_compose(&driver->request, &driver->result);
    if (valid && driver->authority_fd >= 0)
        valid = tp_retirement_store_receipt_authority(&driver->store, driver->authority_fd,
            TP_RETIREMENT_EXECUTION_RECEIPT_PATH, driver->job, driver->attempt, driver->digests[0],
            driver->result.context_sha256, &driver->authority);
    return valid;
}

BUSTER_GLOBAL_LOCAL void driver_artifact(char const* name, TpRetirementComposeArtifact const* artifact, int last)
{
    printf("\"%s\":{\"bytes\":%" PRIu64 ",\"path\":\"%s\",\"sha256\":\"%s\"}%s", name, artifact->bytes, artifact->path,
           artifact->sha256, last ? "" : ",");
}

BUSTER_GLOBAL_LOCAL int driver_main(char const* spec)
{
    Driver* driver = driver_create();
    int valid = driver && driver_parse(driver, spec) && driver_open(driver) && driver_plan(driver) &&
                driver_import_all(driver) && driver_compose(driver);
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
        driver_artifact("retained", &result->retained, 0);
        printf("\"retained_files\":%u,\"seal_entries\":%u,", result->retained_files, result->seal_entries);
        driver_artifact("sealed", &result->sealed, 0);
        driver_artifact("series", &result->series, 0);
        printf("\"series_shards\":%u,", result->series_shards);
        printf("\"untimed_production\":%" PRIu64 ",\"untimed_records\":%" PRIu64, result->untimed_production,
               result->untimed_records);
        if (driver->authority_fd >= 0)
            printf(",\"authority\":{\"attempt\":%" PRIu64 ",\"authority_sha256\":\"%s\",\"job\":\"%s\"}",
                   driver->authority.attempt, driver->authority.authority_sha256, driver->authority.job);
        printf("}\n");
    }
    else fprintf(stderr, "COMPOSE_RESULT refused stage=%s\n",
                 driver && driver->result.refused ? driver->result.refused : "driver");
    driver_destroy(driver);
    return valid ? 0 : 1;
}

/* `canonical FILE` or `context FILE RAW`: the canonical bytes on stdout. */
BUSTER_GLOBAL_LOCAL int driver_canonical(char const* path, char const* raw)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = DRIVER_ARENA_BYTES, .flags = {.no_pool = 1}});
    unsigned char* bytes = NULL;
    size_t length = 0, output_length = 0;
    char* output = NULL;
    int valid = arena && driver_read_file(arena, path, &bytes, &length) &&
                (raw ? tp_retirement_compose_execution_context(bytes, length, raw, arena, &output, &output_length) :
                       tp_retirement_compose_json_canonical(bytes, length, arena, &output, &output_length)) &&
                fwrite(output, 1, output_length, stdout) == output_length && fflush(stdout) == 0;
    if (arena) arena_destroy(arena, 1);
    return valid ? 0 : 1;
}

/* The next line of the series the manifest at `input` lists (its shards are
 * leaves beside it, read in order), or NULL at its end. The stub trusts the
 * manifest; the reviewed adapter and the validator check it. */
BUSTER_GLOBAL_LOCAL char* stub_series_line(FILE* manifest, FILE** shard, char* line, size_t capacity)
{
    char* result = NULL;
    int done = 0;
    while (!done)
    {
        if (*shard && fgets(line, (int)capacity, *shard))
        {
            result = line;
            done = 1;
        }
        else
        {
            char entry[DRIVER_LINE], leaf[DRIVER_PATH];
            if (*shard) fclose(*shard);
            *shard = NULL;
            while (!*shard && fgets(entry, sizeof(entry), manifest))
                if (sscanf(entry, "shard=%*u offset=%*u bytes=%*u sha256=%*s path=%511s", leaf) == 1)
                    *shard = fopen(leaf, "rb");
            done = !*shard;
        }
    }
    return result;
}

/* A stub for the native fixtures only: one structurally valid result per
 * series member, or (per `stub-mode`) a slow run or a wrong family index.
 * The reviewed adapter replaces it in the Python end-to-end test. */
BUSTER_GLOBAL_LOCAL int stub_adapter(char const* input_path, char const* output_path)
{
    char mode[32] = {0};
    FILE* selector = fopen("stub-mode", "rb");
    if (selector)
    {
        if (!fgets(mode, sizeof(mode), selector)) mode[0] = 0;
        fclose(selector);
    }
    if (!strncmp(mode, "slow", 4))
    {
        struct timespec pause = {STUB_SLOW_SECONDS, 0};
        nanosleep(&pause, NULL);
    }
    FILE* input = fopen(input_path, "rb");
    FILE* shard = NULL;
    FILE* output = input ? fopen(output_path, "wb") : NULL;
    int valid = input && output;
    char line[DRIVER_LINE];
    unsigned members = 0, bootstrap = 0, cells = 0, resamples = 0, pairs = 0, seed_version = 0, total = 0;
    unsigned long long seed = 0;
    valid = valid && stub_series_line(input, &shard, line, sizeof(line)) &&
            sscanf(line, "version=%u seed=%llu bootstrap_members=%u cell_members=%u pairs=%u resamples=%u frozen=1 members=%u",
                   &seed_version, &seed, &bootstrap, &cells, &pairs, &resamples, &total) == 7;
    if (valid) fputs("{\"schema\":\"buster-native-retirement-statistics-replay-v1\",\"version\":1,\"members\":[", output);
    while (valid && stub_series_line(input, &shard, line, sizeof(line)))
    {
        char name[TP_RETIREMENT_COMPOSE_MEMBER_BYTES];
        unsigned metric = 0, kind = 0, family = 0, member_cells = 0, member_pairs = 0, member_resamples = 0;
        double limit = 0.0;
        if (sscanf(line, "member=%127s metric=%u kind=%u family=%u cells=%u pairs=%u resamples=%u limit=%lf", name,
                   &metric, &kind, &family, &member_cells, &member_pairs, &member_resamples, &limit) == 8)
        {
            double alpha = 0.05 / (2.0 * 3.0 * 2.0 * (double)(kind ? cells : bootstrap));
            if (!strncmp(mode, "wrong-index", 11) && !members) family += 1;
            fprintf(output, "%s{\"member\":\"%s\",\"metric\":%u,\"kind\":%u,\"family_index\":%u,\"outcome\":\"pass\","
                    "\"valid\":true,\"resampled\":%s,\"resamples\":%u,\"tail_alpha\":%.17g,"
                    "\"round\":[{\"estimate\":1.0,\"lower\":0.99,\"upper\":1.001},"
                    "{\"estimate\":1.0,\"lower\":0.99,\"upper\":1.001}],"
                    "\"pooled\":{\"estimate\":1.0,\"lower\":0.995,\"upper\":1.0005}}",
                    members++ ? "," : "", name, metric, kind, family, kind ? "false" : "true", member_resamples, alpha);
        }
    }
    if (valid) fputs("]}\n", output);
    if (output && fclose(output) != 0) valid = 0;
    if (shard) fclose(shard);
    if (input) fclose(input);
    return valid && members == total ? 0 : 2;
}

/* ---------------------------------------------------------------- fixture */

BUSTER_GLOBAL_LOCAL char const digest_a[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
BUSTER_GLOBAL_LOCAL char const digest_b[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
BUSTER_GLOBAL_LOCAL char const digest_c[] = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
BUSTER_GLOBAL_LOCAL char const digest_e[] = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
BUSTER_GLOBAL_LOCAL char const empty_sha256[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
BUSTER_GLOBAL_LOCAL char self_path[DRIVER_PATH], self_sha256[65];

typedef enum FixtureMutation
{
    FIXTURE_CLEAN,
    FIXTURE_HALVED_WALL,
    FIXTURE_MEMBER_MEMORY
} FixtureMutation;

typedef struct Fixture
{
    char root[96];
    char source[128], store[128], scratch[128], authority[128], queue[128];
    FixtureMutation mutation;
    Driver* driver;
} Fixture;

/* Remove a directory tree with an explicit stack of open directories. */
BUSTER_GLOBAL_LOCAL void fixture_clean(char const* path)
{
    DIR* stack[FIXTURE_DIRECTORY_DEPTH];
    unsigned depth = 0;
    int root = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR* first = root >= 0 ? fdopendir(root) : NULL;
    if (root >= 0 && !first) close(root);
    if (first) stack[depth++] = first;
    while (depth)
    {
        DIR* directory = stack[depth - 1];
        struct dirent* entry = readdir(directory);
        if (!entry)
        {
            closedir(directory);
            --depth;
            /* The now-empty child is removed from its parent's listing. */
        }
        else if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            int parent = dirfd(directory);
            struct stat info;
            int is_directory = fstatat(parent, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(info.st_mode);
            if (is_directory && unlinkat(parent, entry->d_name, AT_REMOVEDIR) != 0 &&
                (errno == ENOTEMPTY || errno == EEXIST) && depth < FIXTURE_DIRECTORY_DEPTH)
            {
                int child = openat(parent, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                DIR* opened = child >= 0 ? fdopendir(child) : NULL;
                if (child >= 0 && !opened) close(child);
                if (opened)
                {
                    stack[depth++] = opened;
                    /* Revisit the parent after the child empties. */
                    rewinddir(directory);
                }
            }
            else if (!is_directory) unlinkat(parent, entry->d_name, 0);
        }
    }
}

BUSTER_GLOBAL_LOCAL int fixture_write(char const* directory, char const* name, void const* bytes, size_t length)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE* output = fopen(path, "wb");
    int valid = output && fwrite(bytes, 1, length, output) == length;
    if (output && fclose(output) != 0) valid = 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL void fixture_digest(void const* bytes, size_t length, char output[65])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, (u64)length);
    sha256_finish_hex(&hash, (char8*)output);
}

BUSTER_GLOBAL_LOCAL int fixture_prior(Fixture* fixture, char const* name, char const* path, char const* body,
                                      char digest[65])
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

/* One object batch's metrics bytes: a header and one member input whose
 * interval and arena high-water become that member's sample. */
BUSTER_GLOBAL_LOCAL size_t fixture_metrics(char* output, size_t capacity, uint64_t interval, uint64_t arena_peak)
{
    size_t used = (size_t)snprintf(output, capacity, "CC_METRICS version=1 schema=buster-cc-metrics inputs=1 records=1 "
        "ok=1 rejected=0 failed=0 not_run=0 prebuilt=0 error=driver.none exit_status=0 action=object "
        "target=x86_64-linux allocator=none compile_jobs=1 compilation_workers=1 intervals=serial keep_going=1 "
        "function_sizes=0 wall_ns=%" PRIu64 " peak_rss_bytes=1048576\nCC_METRICS_INPUT", interval + 20);
    for (unsigned field = 0; field < TP_METRICS_I_COUNT && used < capacity; ++field)
    {
        char const* name = tp_retirement_metrics_input_fields[field];
        unsigned kind = tp_retirement_metrics_kind(name);
        uint64_t number = field == TP_METRICS_I_VERSION || field == TP_METRICS_I_MEASURED ? 1 :
                          field == TP_METRICS_I_START ? 10 : field == TP_METRICS_I_END ? 10 + interval :
                          field == TP_METRICS_I_TOTAL ? interval : field == TP_METRICS_I_ARENA_PEAK ? arena_peak : 0;
        if (kind == 1)
            used += (size_t)snprintf(output + used, capacity - used, " %s=%s", name,
                                     field == TP_METRICS_I_STATUS ? "ok" : "driver.none");
        else if (kind == 2) used += (size_t)snprintf(output + used, capacity - used, " %s=%s", name, empty_sha256);
        else if (kind == 3) used += (size_t)snprintf(output + used, capacity - used, " %s=-", name);
        else used += (size_t)snprintf(output + used, capacity - used, " %s=%" PRIu64, name, number);
    }
    if (used < capacity) used += (size_t)snprintf(output + used, capacity - used, "\n");
    return used < capacity ? used : 0;
}

/* Sample-phase observations by (unit, round, pair, variant). */
typedef struct FixtureObservations
{
    uint64_t row_wall[2][TP_RETIREMENT_ROUNDS][FIXTURE_PAIRS][2], row_memory[2][TP_RETIREMENT_ROUNDS][FIXTURE_PAIRS][2];
    uint64_t runtime[TP_RETIREMENT_ROUNDS][FIXTURE_PAIRS][2];
    uint64_t batch_wall[TP_RETIREMENT_ROUNDS][FIXTURE_PAIRS][2], batch_rss[TP_RETIREMENT_ROUNDS][FIXTURE_PAIRS][2];
} FixtureObservations;

/* One stage's transcript and metrics shard, written with D's own encoders
 * and cursor from `start`; the sample-phase observations are recorded when
 * `seen` is given. Returns the stage's last finish time (0 on failure). */
BUSTER_GLOBAL_LOCAL uint64_t fixture_transcript(Fixture* fixture, char const* tag, uint64_t start,
                                                FixtureObservations* seen)
{
    Driver* driver = fixture->driver;
    unsigned runtime_rows[1] = {1}, workspace[7];
    TpRetirementExecution execution;
    int valid = tp_retirement_execution_init(&execution, driver->statistics.seed, 2, runtime_rows, 1, 3,
                                             FIXTURE_PAIRS, workspace, 7);
    char path[512];
    snprintf(path, sizeof(path), "%s/retirement-execution-%s-0000.jsonl", fixture->source, tag);
    FILE* transcript = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/retirement-metrics-%s-0000.txt", fixture->source, tag);
    FILE* metrics_stream = fopen(path, "w+b");
    TpRetirementMetricsShards metrics;
    valid = valid && transcript && metrics_stream && tp_retirement_metrics_shards_init(&metrics, tag, metrics_stream);
    uint64_t now = start;
    TpRetirementInvocation invocation;
    while (valid && tp_retirement_execution_peek(&execution, &invocation) == TP_RETIREMENT_NEXT_READY)
    {
        uint64_t elapsed = 1000 + invocation.sequence * 37 % 900, rss = 4096 + invocation.sequence % 13 * 64;
        uint64_t interval = elapsed / 2, arena_peak = 65536 + invocation.sequence;
        TpProcessObservation observed = {1000 + invocation.sequence, 7000 + invocation.sequence, now + 1,
                                         now + 1 + elapsed, 1};
        TpProcess process = {0};
        process.wall_seconds = (double)elapsed * 1e-9;
        process.peak_rss_bytes = (double)rss;
        TpRetirementMetricsArtifact artifact;
        int object = !invocation.kind && invocation.group == 0;
        if (object)
        {
            char body[DRIVER_LINE];
            size_t length = fixture_metrics(body, sizeof(body), interval, arena_peak);
            valid = length && tp_retirement_metrics_shards_append(&metrics, (unsigned char const*)body, length, &artifact);
        }
        if (valid && seen && invocation.phase)
        {
            unsigned r = (unsigned)invocation.round, p = (unsigned)invocation.pair, v = invocation.variant;
            if (invocation.kind) seen->runtime[r][p][v] = elapsed;
            else if (object)
            {
                seen->batch_wall[r][p][v] = elapsed;
                seen->batch_rss[r][p][v] = rss;
                seen->row_wall[0][r][p][v] = interval;
                seen->row_memory[0][r][p][v] = arena_peak;
            }
            else
            {
                seen->row_wall[1][r][p][v] = elapsed;
                seen->row_memory[1][r][p][v] = rss;
            }
        }
        TpRetirementOutput output = {digest_a, digest_b, digest_c, object ? &artifact : NULL, 0};
        char line[TP_RETIREMENT_EXECUTION_LINE_CAP];
        size_t count = valid ? tp_retirement_execution_record(line, sizeof(line), &invocation, &observed, &process,
                                                              &output, driver->job, driver->attempt, driver->boot, 2) : 0;
        valid = count && fwrite(line, 1, count, transcript) == count && tp_retirement_execution_commit(&execution, 1);
        now += elapsed + 100;
    }
    TpRetirementShardFile last;
    valid = valid && tp_retirement_execution_complete(&execution) && tp_retirement_metrics_shards_finish(&metrics, &last);
    if (transcript && fclose(transcript) != 0) valid = 0;
    if (metrics_stream && fclose(metrics_stream) != 0) valid = 0;
    return valid ? now : 0;
}

/* Lane D's streams for one A1 layout: an object batch group (census row 0),
 * a singleton link row with runtime (row 1) and one untimed object group
 * (cross-target code row 2), written with D's own encoders and cursor. Every
 * numeric sample is the observation D's collector derives from the same
 * invocation: its supervised interval and RSS, or the member's metrics. The
 * A/A stage (tag `aa`) precedes the bound A/B window. */
BUSTER_GLOBAL_LOCAL int fixture_streams(Fixture* fixture, uint64_t* completed_at)
{
    Driver* driver = fixture->driver;
    FixtureObservations* seen = (FixtureObservations*)tp_retirement_compose_allocate(driver->arena,
                                                                                    sizeof(FixtureObservations));
    uint64_t now = seen && fixture_transcript(fixture, "aa", 1, NULL) ?
                   fixture_transcript(fixture, "ab", driver->bound_at_ns, seen) : 0;
    int valid = now != 0;
    char path[512];
    TpRetirementShardFile last;
    *completed_at = now + 1000;
    /* Row samples: row 0 (object member), then row 1 (singleton, runtime). */
    snprintf(path, sizeof(path), "%s/retirement-samples-0000.jsonl", fixture->source);
    FILE* rows = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/retirement-batches-0000.jsonl", fixture->source);
    FILE* batches = fopen(path, "wb");
    valid = valid && rows && batches;
    for (unsigned unit = 0; valid && unit < 2; ++unit)
        for (unsigned round = 0; valid && round < TP_RETIREMENT_ROUNDS; ++round)
            for (unsigned pair = 0; valid && pair < FIXTURE_PAIRS; ++pair)
            {
                uint64_t values[TP_RETIREMENT_SAMPLE_VALUES] = {seen->row_wall[unit][round][pair][0],
                    seen->row_wall[unit][round][pair][1], seen->row_memory[unit][round][pair][0],
                    seen->row_memory[unit][round][pair][1], unit ? seen->runtime[round][pair][0] : 0,
                    unit ? seen->runtime[round][pair][1] : 0, 0};
                if (fixture->mutation == FIXTURE_HALVED_WALL && !unit && !round && pair == 3) values[1] /= 2;
                if (fixture->mutation == FIXTURE_MEMBER_MEMORY && !unit && round && pair == 5) values[3] += 1;
                char line[TP_RETIREMENT_SAMPLE_LINE_CAP];
                size_t count = tp_retirement_sample_record(line, sizeof(line), unit, round, pair,
                                                           unit ? TP_RETIREMENT_SAMPLE_RUNTIME : 0, values);
                valid = count && fwrite(line, 1, count, rows) == count;
            }
    for (unsigned round = 0; valid && round < TP_RETIREMENT_ROUNDS; ++round)
        for (unsigned pair = 0; valid && pair < FIXTURE_PAIRS; ++pair)
        {
            uint64_t values[TP_RETIREMENT_SAMPLE_VALUES] = {seen->batch_wall[round][pair][0],
                seen->batch_wall[round][pair][1], seen->batch_rss[round][pair][0], seen->batch_rss[round][pair][1],
                0, 0, 0};
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
        char body[DRIVER_LINE];
        size_t length = fixture_metrics(body, sizeof(body), 40, 32768);
        TpRetirementMetricsArtifact artifact;
        char line[TP_RETIREMENT_UNTIMED_LINE_CAP];
        valid = length && tp_retirement_metrics_shards_append(&untimed_metrics, (unsigned char const*)body, length,
                                                              &artifact);
        size_t count = valid ? tp_retirement_untimed_record(line, sizeof(line), &batch, &observed, digest_a, digest_b,
                                                            digest_c, &artifact, driver->job, driver->attempt,
                                                            driver->boot) : 0;
        valid = count && fwrite(line, 1, count, untimed) == count;
    }
    valid = valid && tp_retirement_metrics_shards_finish(&untimed_metrics, &last);
    if (untimed && fclose(untimed) != 0) valid = 0;
    if (untimed_stream && fclose(untimed_stream) != 0) valid = 0;
    /* Retained, unsealed evidence: a lifecycle record and a failure log. */
    valid = valid && fixture_write(fixture->source, "retirement-lifecycle.txt", "lifecycle\n", 10) &&
            fixture_write(fixture->source, "retirement-failure-0000.log", "failure\n", 8);
    return valid;
}

BUSTER_GLOBAL_LOCAL void fixture_row(Driver* driver, unsigned id, unsigned group, unsigned runtime, char const* stage)
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

BUSTER_GLOBAL_LOCAL void fixture_code(Driver* driver, unsigned row, uint64_t baseline, uint64_t candidate)
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

/* The post-A/A binding the context derives from: escapes, non-ASCII text,
 * floats and nested values exercise the canonical writer. */
BUSTER_GLOBAL_LOCAL int fixture_binding(Fixture* fixture, char const* post)
{
    Driver* driver = fixture->driver;
    char body[DRIVER_LINE];
    int length = snprintf(body, sizeof(body),
        "{\"workflow\": {\"records\": {\"oracle\": {\"sha256\": \"%s\"}, \"admission\": {\"sha256\": \"%s\"}},\n"
        " \"phases\": {\"pre_sample_plan\": {\"sha256\": \"%s\"}, \"post_aa_binding\": {\"sha256\": \"%s\"}}},\n"
        " \"support\": {\"root_sha256\": \"%s\", \"files\": []},\n"
        " \"subjects\": {\"candidate\": {\"binary\": \"cand\\u00e9\"}, \"baseline\": {\"binary\": \"base \\\"q\\\"\"}},\n"
        " \"measurement\": {\"ratio\": 1.50, \"list\": [1e2, 0.1, -0, true, null, 12345678901234567890]},\n"
        " \"execution\": {\"service\": \"synthetic\\n\", \"tab\": \"\\t\"}}\n",
        digest_a, digest_b, digest_c, post, digest_e);
    int valid = length > 0 && (size_t)length < sizeof(body) && fixture_write(fixture->store, "binding.json", body,
                                                                               (size_t)length);
    if (valid)
    {
        strcpy(driver->binding, "binding.json");
        fixture_digest(body, (size_t)length, driver->binding_sha256);
    }
    return valid;
}

/* The adapter is this executable; its digest is computed once. */
BUSTER_GLOBAL_LOCAL int fixture_self(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = DRIVER_ARENA_BYTES, .flags = {.no_pool = 1}});
    unsigned char* bytes = NULL;
    size_t length = 0;
    int valid = arena && driver_read_file(arena, self_path, &bytes, &length);
    if (valid) fixture_digest(bytes, length, self_sha256);
    if (arena) arena_destroy(arena, 1);
    return valid;
}

/* A complete driver over fresh private directories; the caller may mutate
 * the request before driver_open/plan/import/compose. */
BUSTER_GLOBAL_LOCAL int fixture_start(Fixture* fixture, FixtureMutation mutation)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->mutation = mutation;
    strcpy(fixture->root, "/tmp/buster-retirement-compose-XXXXXX");
    int valid = mkdtemp(fixture->root) != NULL;
    char const* names[] = {"source", "store", "scratch", "authority", "queue"};
    char* paths[] = {fixture->source, fixture->store, fixture->scratch, fixture->authority, fixture->queue};
    for (unsigned i = 0; valid && i < BUSTER_ARRAY_LENGTH(names); ++i)
    {
        snprintf(paths[i], 128, "%s/%s", fixture->root, names[i]);
        valid = mkdir(paths[i], 0700) == 0;
    }
    fixture->driver = valid ? driver_create() : NULL;
    valid = valid && fixture->driver;
    Driver* driver = fixture->driver;
    if (valid)
    {
        strcpy(driver->source, fixture->source);
        strcpy(driver->store_path, fixture->store);
        strcpy(driver->scratch, fixture->scratch);
        strcpy(driver->adapter, self_path);
        strcpy(driver->adapter_sha256, self_sha256);
        strcpy(driver->authority_path, fixture->authority);
        strcpy(driver->job, "job-7");
        strcpy(driver->boot, "boot-a");
        strcpy(driver->aa_tag, "aa");
        strcpy(driver->sealed, "retirement-sealed-result.json");
        driver->attempt = 3;
        /* After the A/A stage's window (which starts at 1). */
        driver->bound_at_ns = UINT64_C(10000000);
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
        driver_views(driver);
        TpRetirementComposeShape shape = {&driver->layout, FIXTURE_PAIRS, driver->code_count, 4};
        TpRetirementComposeBounds bounds;
        valid = tp_retirement_compose_bounds(&shape, &bounds);
        driver->statistics = (TpRetirementPlan){.seed = 20260929, .version = TP_RETIREMENT_STATISTICS_VERSION,
            .bootstrap_members_per_scope = bounds.bootstrap_members, .cell_members_per_scope = bounds.cell_members,
            .pairs_per_round = FIXTURE_PAIRS, .resamples = TP_RETIREMENT_MIN_RESAMPLES, .frozen_before_samples = 1};
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
                fixture_prior(fixture, "workflow.phases.post_aa_binding", "post.json", "{\"post\":1}\n", driver->digests[4]) &&
                fixture_binding(fixture, driver->digests[4]);
        TpRetirementComposePartition* rows = &driver->partitions[0][0];
        TpRetirementComposePartition* batches = &driver->partitions[1][0];
        strcpy(rows->identity, "rows-0");
        strcpy(rows->path, "retirement-rows-manifest.json");
        rows->records = 2 * TP_RETIREMENT_ROUNDS * FIXTURE_PAIRS;
        strcpy(batches->identity, "batches-0");
        strcpy(batches->path, "retirement-batches-manifest.json");
        batches->records = TP_RETIREMENT_ROUNDS * FIXTURE_PAIRS;
        driver->partition_counts[0] = driver->partition_counts[1] = 1;
        /* The A/A stage is retained evidence (copies suffice here: the
         * composer never reads it), plus a lifecycle record and a failure
         * log group. */
        valid = valid && driver_list_add(&driver->transcript, "retirement-execution-ab-0000.jsonl", NULL) &&
                driver_list_add(&driver->samples[0], "retirement-samples-0000.jsonl", NULL) &&
                driver_list_add(&driver->samples[1], "retirement-batches-0000.jsonl", NULL) &&
                driver_list_add(&driver->metrics, "retirement-metrics-ab-0000.txt", NULL) &&
                driver_list_add(&driver->untimed, "retirement-untimed-batches.jsonl", NULL) &&
                driver_list_add(&driver->untimed_metrics, "retirement-metrics-untimed-0000.txt", NULL) &&
                driver_retain(driver, "transcript", "retirement-execution-aa-0000.jsonl", NULL, 1, "reserved") &&
                driver_list_add(&driver->retained_files, "retirement-execution-aa-0000.jsonl", NULL) &&
                driver_list_add(&driver->aa_transcript, "retirement-execution-aa-0000.jsonl", NULL) &&
                driver_retain(driver, "samples", "retirement-samples-aa-0000.jsonl", NULL, 1, "reserved") &&
                driver_list_add(&driver->retained_files, "retirement-samples-aa-0000.jsonl", "retirement-samples-0000.jsonl") &&
                driver_retain(driver, "samples", "retirement-batches-aa-0000.jsonl", NULL, 1, "reserved") &&
                driver_list_add(&driver->retained_files, "retirement-batches-aa-0000.jsonl", "retirement-batches-0000.jsonl") &&
                driver_retain(driver, "metrics", "retirement-metrics-aa-", ".txt", 4, "reserved") &&
                driver_list_add(&driver->retained_files, "retirement-metrics-aa-0000.txt", NULL) &&
                driver_retain(driver, "record", "retirement-lifecycle.txt", NULL, 1, "4096") &&
                driver_list_add(&driver->retained_files, "retirement-lifecycle.txt", NULL) &&
                driver_retain(driver, "log", "retirement-failure-", ".log", 4, "4096") &&
                driver_list_add(&driver->retained_files, "retirement-failure-0000.log", NULL);
    }
    tp_retirement_store_test_fail_sync = 0;
    tp_retirement_store_test_sync_calls = 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL void fixture_stop(Fixture* fixture)
{
    driver_destroy(fixture->driver);
    fixture->driver = NULL;
    if (fixture->root[0])
    {
        fixture_clean(fixture->root);
        CHECK(rmdir(fixture->root) == 0);
    }
    tp_retirement_store_test_fail_sync = 0;
}

BUSTER_GLOBAL_LOCAL int fixture_ready(Fixture* fixture)
{
    int valid = fixture_start(fixture, FIXTURE_CLEAN) && driver_open(fixture->driver) && driver_plan(fixture->driver) &&
                driver_import_all(fixture->driver);
    return valid;
}

/* ------------------------------------------------------------------ tests */

BUSTER_GLOBAL_LOCAL int file_contains(Arena* arena, int root, char const* path, char const* needle)
{
    enum { FILE_CONTAINS_BYTES = 1 << 22 };
    int fd = openat(root, path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    char* buffer = (char*)tp_retirement_compose_allocate(arena, FILE_CONTAINS_BYTES);
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
    return found;
}

/* One canonical-writer vector: the expected bytes, or NULL for a refusal. */
BUSTER_GLOBAL_LOCAL int canonical_is(Arena* arena, char const* input, char const* expected)
{
    char* output = NULL;
    size_t length = 0;
    int valid = tp_retirement_compose_json_canonical((unsigned char const*)input, strlen(input), arena, &output, &length);
    int matched = expected ? valid && length == strlen(expected) && !memcmp(output, expected, length) : !valid;
    if (!matched) fprintf(stderr, "COMPOSE_TEST canonical input=%s output=%.*s\n", input, valid ? (int)length : 0,
                          valid ? output : "");
    return matched;
}

BUSTER_GLOBAL_LOCAL void test_canonical_json(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = DRIVER_ARENA_BYTES, .flags = {.no_pool = 1}});
    CHECK(arena != NULL);
    CHECK(canonical_is(arena, "{\"b\":1,\"a\":[1.0,-0,1e16,0.0001,1E-5,\"x\\u00e9\\n\\\"\",true,null]}",
                       "{\"a\":[1.0,0,1e+16,0.0001,1e-05,\"x\xc3\xa9\\n\\\"\",true,null],\"b\":1}"));
    CHECK(canonical_is(arena, " { \"z\" : { \"b\" : 2 , \"a\" : 1 } , \"y\" : [ { \"d\" : 0 , \"c\" : -1.5 } ] } ",
                       "{\"y\":[{\"c\":-1.5,\"d\":0}],\"z\":{\"a\":1,\"b\":2}}"));
    CHECK(canonical_is(arena, "[\"\\ud83d\\ude00\",\"\\u0001\\u001f\\t\\/\",\"\x7f\"]",
                       "[\"\xf0\x9f\x98\x80\",\"\\u0001\\u001f\\t/\",\"\x7f\"]"));
    CHECK(canonical_is(arena, "{\"\xc3\xa9\":1,\"z\":2,\"\":3}", "{\"\":3,\"z\":2,\"\xc3\xa9\":1}"));
    CHECK(canonical_is(arena, "[123456789012345678901234567890,-7,0.1,100.0,1.5e300,5e-324,123456789012345680.0,-0.0,1e5]",
                       "[123456789012345678901234567890,-7,0.1,100.0,1.5e+300,5e-324,1.2345678901234568e+17,-0.0,"
                       "100000.0]"));
    CHECK(canonical_is(arena, "{}", "{}"));
    CHECK(canonical_is(arena, "[[],{}]", "[[],{}]"));
    /* Refusals: duplicate keys, lone surrogates, raw controls, non-finite or
     * malformed numbers, trailing commas, bad UTF-8 and trailing bytes. */
    CHECK(canonical_is(arena, "{\"a\":1,\"a\":2}", NULL));
    CHECK(canonical_is(arena, "[\"\\ud800\"]", NULL));
    CHECK(canonical_is(arena, "[\"\\udc00\"]", NULL));
    CHECK(canonical_is(arena, "[\"a\x01\"]", NULL));
    CHECK(canonical_is(arena, "[NaN]", NULL));
    CHECK(canonical_is(arena, "[1e400]", NULL));
    CHECK(canonical_is(arena, "[01]", NULL));
    CHECK(canonical_is(arena, "[1.]", NULL));
    CHECK(canonical_is(arena, "[1,]", NULL));
    CHECK(canonical_is(arena, "{\"a\":1,}", NULL));
    CHECK(canonical_is(arena, "[\"\xc0\xaf\"]", NULL));
    CHECK(canonical_is(arena, "[\"\xed\xa0\x80\"]", NULL));
    CHECK(canonical_is(arena, "[1] 2", NULL));
    CHECK(canonical_is(arena, "", NULL));
    char deep[601];
    memset(deep, '[', 300);
    memset(deep + 300, ']', 300);
    deep[600] = 0;
    CHECK(canonical_is(arena, deep, NULL));
    /* Python's repr(float) at the fixed/exponent boundaries. */
    char repr[TP_RETIREMENT_COMPOSE_REPR_BYTES];
    CHECK(tp_retirement_compose_float_repr(1e16, repr) && !strcmp(repr, "1e+16"));
    CHECK(tp_retirement_compose_float_repr(9999999999999998.0, repr) && !strcmp(repr, "9999999999999998.0"));
    CHECK(tp_retirement_compose_float_repr(0.0001, repr) && !strcmp(repr, "0.0001"));
    CHECK(tp_retirement_compose_float_repr(0.00001, repr) && !strcmp(repr, "1e-05"));
    CHECK(tp_retirement_compose_float_repr(-2.5, repr) && !strcmp(repr, "-2.5"));
    if (arena) arena_destroy(arena, 1);
}

BUSTER_GLOBAL_LOCAL void test_compose_success(void)
{
    Fixture fixture;
    CHECK(fixture_ready(&fixture));
    Driver* driver = fixture.driver;
    CHECK(driver_compose(driver));
    TpRetirementComposeResult const* result = &driver->result;
    /* (G + U) * 2 * (warmups + rounds * pairs) = 3 * 2 * 122. */
    CHECK(result->invocations == 732);
    /* A small family's series is one canonical shard (#1880). */
    CHECK(result->series_shards == 1);
    CHECK(result->seal_entries == 4 + 5 + 1 + 1 + 1 + 1 + 2 + 2 + result->series_shards);
    CHECK(result->retained_files == 6 && result->untimed_records == 2 && result->untimed_production == 0);
    CHECK(tp_retirement_store_validate(&driver->store));
    CHECK(driver->store.count == driver->store.planned_files);
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_EXECUTION_RECEIPT_PATH, result->context_sha256));
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_COMPOSE_BUNDLE_PATH,
                        result->raw_measurements_sha256));
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_COMPOSE_BUNDLE_PATH,
                        "\"untimed_batches\":{\"bytes\":"));
    CHECK(file_contains(driver->arena, driver->store_fd, "retirement-sealed-result.json",
                        "\"name\":\"untimed.metrics_shard.0\""));
    CHECK(file_contains(driver->arena, driver->store_fd, "retirement-sealed-result.json",
                        "\"name\":\"result_input.shard.batches-0000\""));
    CHECK(file_contains(driver->arena, driver->store_fd, "retirement-rows-manifest.json", "\"identity\":\"samples-0000\""));
    CHECK(file_contains(driver->arena, driver->store_fd, "retirement-statistics-series-0000.txt",
                        "member=generated_runtime/cell/row=1 metric=2 kind=1"));
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_COMPOSE_SERIES_PATH,
                        "BQ-RETIREMENT-STATISTICS-SERIES-V1\nseries bytes="));
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_COMPOSE_SERIES_PATH,
                        "shard_bytes=67108864\nshard=0 offset=0 bytes="));
    CHECK(file_contains(driver->arena, driver->store_fd, "retirement-sealed-result.json",
                        "\"name\":\"workflow.adapter_input.shard.0\",\"path\":\"retirement-statistics-series-0000.txt\""));
    /* Code summary: rows 0..2 with 101/100, 200/200 and 299/300. */
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_COMPOSE_BUNDLE_PATH,
                        "\"aggregate_pass\":true,\"aggregate_ratio\":1.0,\"per_cell_max_ratio\":1.01,\"per_cell_pass\":true"));
    /* The retained manifest lists every unsealed file, in path order, and
     * the producer authority binds its digest. */
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_RETAINED_MANIFEST_PATH,
                        "retirement-batches-aa-0000.jsonl\ntranscript "));
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_RETAINED_MANIFEST_PATH,
                        " 8 retirement-failure-0000.log\n"));
    CHECK(!file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_RETAINED_MANIFEST_PATH,
                         "retirement-execution-ab-0000.jsonl"));
    CHECK(!strcmp(driver->authority.retained_sha256, result->retained.sha256));
    CHECK(driver->authority.authority_sha256[0] != 0);
    /* The context is the canonical _execution_context of binding.json. */
    char const expected_context_prefix[] = "{\"admission_sha256\":\"bbbb";
    char* context = NULL;
    size_t context_length = 0;
    unsigned char* binding = NULL;
    size_t binding_length = 0;
    char path[512], context_sha256[65];
    snprintf(path, sizeof(path), "%s/binding.json", fixture.store);
    CHECK(driver_read_file(driver->arena, path, &binding, &binding_length) &&
          tp_retirement_compose_execution_context(binding, binding_length, result->raw_measurements_sha256,
                                                  driver->arena, &context, &context_length));
    if (context)
    {
        fixture_digest(context, context_length, context_sha256);
        CHECK(!strcmp(context_sha256, result->context_sha256));
        CHECK(!strncmp(context, expected_context_prefix, strlen(expected_context_prefix)));
        CHECK(strstr(context, "\"baseline\":{\"binary\":\"base \\\"q\\\"\"},\"candidate\":{\"binary\":\"cand\xc3\xa9\"},"
                              "\"execution\":{\"service\":\"synthetic\\n\",\"tab\":\"\\t\"},\"measurement\":{\"list\":"
                              "[100.0,0.1,0,true,null,12345678901234567890],\"ratio\":1.5}") != NULL);
    }
    /* The scratch copy is removed, and a second composition cannot
     * overwrite the sealed outputs. */
    struct stat info;
    CHECK(fstatat(driver->scratch_fd, TP_RETIREMENT_COMPOSE_SERIES_PATH, &info, 0) != 0);
    CHECK(fstatat(driver->scratch_fd, "retirement-statistics-series-0000.txt", &info, 0) != 0);
    TpRetirementComposeResult again;
    CHECK(!tp_retirement_compose(&driver->request, &again));
    fixture_stop(&fixture);
}

/* (#1880) With a small shard size the same family spans several canonical
 * shards: the plan reserves the bound, the settle keeps exactly the shards
 * written, and every shard is sealed and listed in order. */
BUSTER_GLOBAL_LOCAL void test_compose_series_shards(void)
{
    Fixture fixture;
    tp_retirement_compose_test_shard_bytes = 4096;
    CHECK(fixture_ready(&fixture));
    Driver* driver = fixture.driver;
    CHECK(driver_compose(driver));
    tp_retirement_compose_test_shard_bytes = 0;
    TpRetirementComposeResult const* result = &driver->result;
    CHECK(result->series_shards > 2);
    CHECK(result->seal_entries == 4 + 5 + 1 + 1 + 1 + 1 + 2 + 2 + result->series_shards);
    CHECK(tp_retirement_store_validate(&driver->store));
    CHECK(driver->store.count == driver->store.planned_files);
    uint64_t total = 0;
    for (unsigned s = 0; s < result->series_shards; ++s)
    {
        char path[TP_RETIREMENT_STORE_PATH_BYTES + 1], line[128];
        snprintf(path, sizeof(path), "retirement-statistics-series-%04u.txt", s);
        struct stat info;
        CHECK(fstatat(driver->store_fd, path, &info, 0) == 0 && info.st_size > 0 && info.st_size <= 4096);
        snprintf(line, sizeof(line), "shard=%u offset=%" PRIu64 " bytes=%lld ", s, total, (long long)info.st_size);
        CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_COMPOSE_SERIES_PATH, line));
        snprintf(line, sizeof(line), "\"name\":\"workflow.adapter_input.shard.%u\"", s);
        CHECK(file_contains(driver->arena, driver->store_fd, "retirement-sealed-result.json", line));
        total += (uint64_t)info.st_size;
    }
    char series[96];
    snprintf(series, sizeof(series), "series bytes=%" PRIu64 " ", total);
    CHECK(file_contains(driver->arena, driver->store_fd, TP_RETIREMENT_COMPOSE_SERIES_PATH, series));
    CHECK(file_contains(driver->arena, driver->store_fd, "retirement-statistics-series-0000.txt", "version=1 seed="));
    fixture_stop(&fixture);
}

typedef enum Refusal
{
    REFUSE_MISSING_SHARD, REFUSE_EXTRA_FILE, REFUSE_EXTRA_METRICS, REFUSE_PRIOR_DIGEST, REFUSE_BINDING_DIGEST,
    REFUSE_JOB, REFUSE_PLAN_DIGEST, REFUSE_FAMILY_COUNT, REFUSE_UNPLANNED, REFUSE_PARTITION, REFUSE_TRANSCRIPT_ORDER,
    REFUSE_SAMPLE_ORDER, REFUSE_UNTIMED_MISSING, REFUSE_ADAPTER, REFUSE_WINDOW, REFUSE_DROPPED_RETAINED,
    REFUSE_DECLARATION_CHANGED, REFUSE_HALVED_WALL, REFUSE_MEMBER_MEMORY, REFUSE_ADAPTER_DIGEST,
    REFUSE_ADAPTER_TIMEOUT, REFUSE_ADAPTER_OUTPUT, REFUSE_GROUP_GAP, REFUSE_BINDING_POST, REFUSE_PRIOR_COUNT,
    REFUSE_AA_TRAILING_SHARD, REFUSE_AA_TRANSCRIPT_TAMPERED, REFUSE_COUNT
} Refusal;

BUSTER_GLOBAL_LOCAL char const* const refusal_stages[REFUSE_COUNT] = {"inventory", "inventory", "transcript", "prior",
    "context", "transcript", "prior", "bounds", "request", "partitions", "transcript", "samples", "untimed", "adapter",
    "transcript", "inventory", "retained", "samples", "samples", "adapter", "adapter", "adapter", "inventory", "context",
    "prior", "aa-transcript", "aa-transcript"};

/* Swap two lines of a source stream file before import. */
BUSTER_GLOBAL_LOCAL int fixture_swap_lines(Fixture* fixture, char const* name, unsigned first, unsigned second)
{
    Arena* arena = fixture->driver->arena;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", fixture->source, name);
    unsigned char* bytes = NULL;
    size_t length = 0;
    int valid = driver_read_file(arena, path, &bytes, &length);
    size_t* starts = valid ? (size_t*)tp_retirement_compose_allocate(arena, (uint64_t)(length + 1) * sizeof(size_t)) : NULL;
    unsigned lines = 0;
    valid = valid && starts;
    for (size_t i = 0; valid && i < length; ++i)
        if (!i || bytes[i - 1] == '\n') starts[lines++] = i;
    valid = valid && first < second && second < lines;
    unsigned char* copy = valid ? (unsigned char*)tp_retirement_compose_allocate(arena, length) : NULL;
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
    return valid;
}

BUSTER_GLOBAL_LOCAL void test_compose_refusal(Refusal refusal)
{
    Fixture fixture;
    FixtureMutation mutation = refusal == REFUSE_HALVED_WALL ? FIXTURE_HALVED_WALL :
                               refusal == REFUSE_MEMBER_MEMORY ? FIXTURE_MEMBER_MEMORY : FIXTURE_CLEAN;
    int started = fixture_start(&fixture, mutation);
    CHECK(started);
    Driver* driver = fixture.driver;
    int ready = started;
    if (ready && refusal == REFUSE_EXTRA_METRICS)
        ready = fixture_write(fixture.source, "retirement-metrics-ab-0001.txt", "orphan\n", 7) &&
                driver_list_add(&driver->metrics, "retirement-metrics-ab-0001.txt", NULL);
    if (ready && refusal == REFUSE_EXTRA_FILE)
        ready = fixture_write(fixture.source, "retirement-extra.log", "log\n", 4) &&
                driver_list_add(&driver->retained_files, "retirement-extra.log", NULL);
    /* A second A/A metrics shard that no A/A artifact reaches (a trailing,
     * never-referenced publication) passes the declaration but not tiling. */
    if (ready && refusal == REFUSE_AA_TRAILING_SHARD)
        ready = fixture_write(fixture.source, "retirement-metrics-aa-0001.txt", "CC_METRICS trailing\n", 20) &&
                driver_list_add(&driver->retained_files, "retirement-metrics-aa-0001.txt", NULL);
    /* The A/A transcript names the A/B shard: its artifacts do not tile. */
    if (ready && refusal == REFUSE_AA_TRANSCRIPT_TAMPERED)
        ready = driver_list_remove(&driver->retained_files, "retirement-execution-aa-0000.jsonl") &&
                driver_list_add(&driver->retained_files, "retirement-execution-aa-0000.jsonl",
                                "retirement-execution-ab-0000.jsonl");
    if (ready && refusal == REFUSE_GROUP_GAP)
        ready = fixture_write(fixture.source, "retirement-failure-0002.log", "log\n", 4) &&
                driver_list_add(&driver->retained_files, "retirement-failure-0002.log", NULL);
    if (ready && refusal == REFUSE_DROPPED_RETAINED)
        ready = driver_list_remove(&driver->retained_files, "retirement-execution-aa-0000.jsonl");
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
        ready = driver_read_file(driver->arena, path, &bytes, &length);
        unsigned char* newline = ready ? (unsigned char*)memchr(bytes, '\n', length) : NULL;
        ready = newline && fixture_write(fixture.source, "retirement-untimed-batches.jsonl", bytes,
                                         (size_t)(newline - bytes) + 1);
    }
    if (ready && refusal == REFUSE_BINDING_POST) ready = fixture_binding(&fixture, digest_c);
    if (ready && refusal == REFUSE_ADAPTER_TIMEOUT)
    {
        ready = fixture_write(fixture.scratch, "stub-mode", "slow\n", 5);
        driver->timeout_ns = FIXTURE_ADAPTER_TIMEOUT_NS;
    }
    if (ready && refusal == REFUSE_ADAPTER_OUTPUT) ready = fixture_write(fixture.scratch, "stub-mode", "wrong-index\n", 12);
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
        case REFUSE_PRIOR_DIGEST:
            driver->prior_storage[0].sha256[0] = driver->prior_storage[0].sha256[0] == 'f' ? 'e' : 'f';
            break;
        case REFUSE_BINDING_DIGEST:
            driver->binding_sha256[0] = driver->binding_sha256[0] == 'f' ? 'e' : 'f';
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
            driver->partitions[0][0].records -= 1;
            break;
        case REFUSE_ADAPTER:
            strcpy(driver->adapter, "/nonexistent/retirement-replay");
            break;
        case REFUSE_DECLARATION_CHANGED:
            driver->retained[4].bytes_max += 1;
            break;
        case REFUSE_ADAPTER_DIGEST:
            driver->adapter_sha256[0] = driver->adapter_sha256[0] == 'f' ? 'e' : 'f';
            break;
        case REFUSE_PRIOR_COUNT:
            --driver->prior_count;
            break;
        default:
            break;
        }
        CHECK(!driver_compose(driver));
        CHECK(driver->result.refused && !strcmp(driver->result.refused, refusal_stages[refusal]));
        if (driver->result.refused && strcmp(driver->result.refused, refusal_stages[refusal]))
            fprintf(stderr, "COMPOSE_TEST refusal=%u stage=%s\n", (unsigned)refusal, driver->result.refused);
        /* The refused attempt is poisoned and published no sealed result. */
        CHECK(driver->store.failed);
        struct stat info;
        CHECK(fstatat(driver->store_fd, "retirement-sealed-result.json", &info, AT_SYMLINK_NOFOLLOW) != 0);
    }
    fixture_stop(&fixture);
}

BUSTER_GLOBAL_LOCAL void test_compose_refusals(void)
{
    for (unsigned refusal = 0; refusal < REFUSE_COUNT; ++refusal) test_compose_refusal((Refusal)refusal);
}

BUSTER_GLOBAL_LOCAL int reopen_store(Driver* driver)
{
    tp_retirement_store_close(&driver->store);
    int valid = tp_retirement_store_open(&driver->store, driver->store_fd, driver->files, TP_RETIREMENT_STORE_FILES);
    return valid;
}

/* (#1880) The A1 production family at both pair bounds: 16 native-host
 * configurations of five object groups (398, 4, 1, 1 and 1 members; the
 * compiler-default batch carries 416 inputs) and a runtime-eligible native
 * link and self-host singleton: 6,482 timed rows, 80 object groups and
 * 13,126 #619 cells, with 880 untimed cross-target object groups. Its series
 * exceeds one store file; as shards it fits, and the plan reserves both
 * campaign stages and every composer output within the 4,096-entry and
 * 128 GiB store (retirement_capacity_test.py mirrors these bounds). */
BUSTER_GLOBAL_LOCAL void test_production_capacity(void)
{
    enum { CONFIGURATIONS = 16, RECIPES = 5, OBJECT_GROUPS = CONFIGURATIONS * RECIPES, ROWS = 6482, UNTIMED = 880 };
    static unsigned const members[RECIPES] = {398, 4, 1, 1, 1};
    static unsigned const inputs[RECIPES] = {416, 4, 1, 1, 1};
    static char const* const allocators[4] = {"a0", "a1", "a2", "a3"};
    static char const* const frontends[2] = {"direct-ssa", "mir"};
    static char const* const pic[2] = {"0", "1"};
    static uint64_t const expected_series[2] = {UINT64_C(406659336), UINT64_C(1710438664)};
    static unsigned const expected_shards[2] = {7, 26};
    Arena* arena = arena_create((ArenaCreation){.reserved_size = DRIVER_ARENA_BYTES, .flags = {.no_pool = 1}});
    TpRetirementComposeRow* rows = arena ? (TpRetirementComposeRow*)tp_retirement_compose_allocate(arena,
                                        ROWS * sizeof(TpRetirementComposeRow)) : NULL;
    unsigned* kinds = arena ? (unsigned*)tp_retirement_compose_allocate(arena,
                                  (OBJECT_GROUPS + 2) * sizeof(unsigned)) : NULL;
    CHECK(rows && kinds);
    unsigned count = 0;
    uint64_t metrics_bytes = 0, untimed_metrics_bytes = 0, untimed_rows = 0;
    for (unsigned c = 0; rows && kinds && c < CONFIGURATIONS; ++c)
    {
        for (unsigned k = 0; k < RECIPES; ++k)
        {
            unsigned group = c * RECIPES + k;
            kinds[group] = TP_RETIREMENT_GROUP_OBJECT;
            metrics_bytes += 4096 + (uint64_t)inputs[k] * 4096;
            for (unsigned m = 0; m < members[k]; ++m, ++count)
                rows[count] = (TpRetirementComposeRow){count, group, 0, {"x86_64-unknown-linux-gnu", "baseline",
                    allocators[c % 4], frontends[(c / 4) % 2], pic[c / 8], "object"}};
        }
    }
    /* The untimed groups repeat the object shapes on 11 cross targets. */
    for (unsigned g = 0; g < UNTIMED; ++g)
    {
        untimed_metrics_bytes += 4096 + (uint64_t)members[g % RECIPES] * 4096;
        untimed_rows += members[g % RECIPES];
    }
    if (rows && kinds)
    {
        rows[count] = (TpRetirementComposeRow){count, OBJECT_GROUPS, 1, {"x86_64-unknown-linux-gnu", "baseline", "a0",
                                                                         "direct-ssa", "0", "link"}};
        kinds[OBJECT_GROUPS] = TP_RETIREMENT_GROUP_SINGLETON;
        ++count;
        rows[count] = (TpRetirementComposeRow){count, OBJECT_GROUPS + 1, 1, {"x86_64-unknown-linux-gnu", "baseline",
                                                                             "a0", "direct-ssa", "0",
                                                                             "self-host-stage1"}};
        kinds[OBJECT_GROUPS + 1] = TP_RETIREMENT_GROUP_SINGLETON;
        ++count;
    }
    CHECK(count == ROWS && untimed_rows == 11 * 16 * 405);
    Fixture fixture;
    CHECK(fixture_start(&fixture, FIXTURE_CLEAN) && driver_open(fixture.driver));
    Driver* driver = fixture.driver;
    driver_views(driver);
    TpRetirementComposeLayout layout = {rows, kinds, ROWS, OBJECT_GROUPS + 2, 78912, UNTIMED};
    unsigned pair_counts[2] = {TP_RETIREMENT_MIN_PAIRS_PER_ROUND, TP_RETIREMENT_EXECUTION_MAX_PAIRS};
    for (unsigned p = 0; rows && kinds && count == ROWS && p < 2; ++p)
    {
        TpRetirementComposeShape shape = {&layout, pair_counts[p], (unsigned)(ROWS + untimed_rows), driver->prior_count};
        TpRetirementComposeBounds bounds;
        CHECK(tp_retirement_compose_bounds(&shape, &bounds));
        CHECK(bounds.cell_members == 13126 && bounds.bootstrap_members == 60 && bounds.members == 13186);
        CHECK(bounds.series == expected_series[p] && bounds.series > TP_RETIREMENT_STORE_FILE_BYTES);
        CHECK(bounds.series_shards == expected_shards[p]);
        CHECK(bounds.files == 2 + TP_RETIREMENT_COMPOSE_FIXED_OUTPUTS + expected_shards[p]);
        CHECK(bounds.series_manifest <= TP_RETIREMENT_STORE_FILE_BYTES && bounds.code <= TP_RETIREMENT_STORE_FILE_BYTES &&
              bounds.replay <= TP_RETIREMENT_STORE_FILE_BYTES && bounds.seal <= TP_RETIREMENT_STORE_FILE_BYTES);
        TpRetirementCampaignShape campaign = {OBJECT_GROUPS + 2, OBJECT_GROUPS, ROWS, 2, pair_counts[p], UNTIMED,
                                              UNTIMED, metrics_bytes, untimed_metrics_bytes, 4096 + 416 * 4096};
        TpRetirementCampaignCapacity capacity;
        TpRetirementCampaignStorePlan plan = {0};
        CHECK(tp_retirement_campaign_capacity(&campaign, &capacity));
        CHECK(reopen_store(driver));
        CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &shape, &driver->declaration,
                                         DRIVER_EXTERNAL_ENTRIES, 0, &plan));
        CHECK(plan.entries <= TP_RETIREMENT_STORE_FILES && plan.bytes <= TP_RETIREMENT_STORE_TOTAL_BYTES);
        CHECK(driver->store.bounded_files >= capacity.total_metrics_shards_upper_bound + bounds.series_shards - 1);
        printf("COMPOSE_CAPACITY pairs=%u rows=%u object_groups=%u cells=%u series=%" PRIu64 " series_shards=%u "
               "composer_files=%u store_entries=%" PRIu64 "/%u store_bytes=%" PRIu64 "/%" PRIu64 "\n",
               pair_counts[p], (unsigned)ROWS, (unsigned)OBJECT_GROUPS, bounds.cell_members, bounds.series,
               bounds.series_shards, bounds.files, plan.entries, TP_RETIREMENT_STORE_FILES, plan.bytes,
               TP_RETIREMENT_STORE_TOTAL_BYTES);
    }
    fixture_stop(&fixture);
    if (arena) arena_destroy(arena, 1);
}

BUSTER_GLOBAL_LOCAL void test_budget_and_settle(void)
{
    /* (#1880) A family whose series exceeds the per-file store cap is bounded
     * as several shards; one needing more than the shard cap is refused
     * before any timing. */
    enum { LARGE_ROWS = 4000 };
    Arena* arena = arena_create((ArenaCreation){.reserved_size = DRIVER_ARENA_BYTES, .flags = {.no_pool = 1}});
    TpRetirementComposeRow* rows = arena ? (TpRetirementComposeRow*)tp_retirement_compose_allocate(arena,
                                        LARGE_ROWS * sizeof(TpRetirementComposeRow)) : NULL;
    unsigned* kinds = arena ? (unsigned*)tp_retirement_compose_allocate(arena, LARGE_ROWS * sizeof(unsigned)) : NULL;
    CHECK(rows && kinds);
    if (rows && kinds)
    {
        char const* values[TP_RETIREMENT_COMPOSE_DIMENSIONS] = {"x86_64-unknown-linux-gnu", "baseline", "none",
                                                               "direct-ssa", "0", "link"};
        for (unsigned i = 0; i < LARGE_ROWS; ++i)
        {
            rows[i] = (TpRetirementComposeRow){i, i, i == 0, {values[0], values[1], values[2], values[3], values[4],
                                                              values[5]}};
            kinds[i] = TP_RETIREMENT_GROUP_SINGLETON;
        }
        kinds[1] = TP_RETIREMENT_GROUP_OBJECT;
        rows[1].dimensions[5] = "object";
        TpRetirementComposeLayout large = {rows, kinds, LARGE_ROWS, LARGE_ROWS, LARGE_ROWS, 0};
        TpRetirementComposeShape shape = {&large, TP_RETIREMENT_EXECUTION_MAX_PAIRS, 1, 1};
        TpRetirementComposeBounds bounds;
        CHECK(tp_retirement_compose_bounds(&shape, &bounds));
        CHECK(bounds.series > TP_RETIREMENT_STORE_FILE_BYTES && bounds.series_shards > 1 &&
              bounds.series_shards <= TP_RETIREMENT_COMPOSE_SERIES_SHARDS &&
              bounds.files == bounds.manifest_count + TP_RETIREMENT_COMPOSE_FIXED_OUTPUTS + bounds.series_shards);
        tp_retirement_compose_test_shard_bytes = 4096;
        CHECK(!tp_retirement_compose_bounds(&shape, &bounds));
        tp_retirement_compose_test_shard_bytes = 0;
        shape.pairs = FIXTURE_PAIRS;
        TpRetirementComposeLayout small = {rows, kinds, 2, 2, LARGE_ROWS, 0};
        shape.layout = &small;
        CHECK(tp_retirement_compose_bounds(&shape, &bounds));
        shape.pairs = FIXTURE_PAIRS + 1;
        CHECK(!tp_retirement_compose_bounds(&shape, &bounds));
        /* A layout with no runtime row has no generated-runtime cells. */
        rows[0].runtime = 0;
        shape.pairs = FIXTURE_PAIRS;
        CHECK(!tp_retirement_compose_bounds(&shape, &bounds));
        rows[0].runtime = 1;
    }
    if (arena) arena_destroy(arena, 1);

    Fixture fixture;
    CHECK(fixture_start(&fixture, FIXTURE_CLEAN) && driver_open(fixture.driver));
    Driver* driver = fixture.driver;
    TpRetirementCampaignCapacity capacity;
    TpRetirementCampaignStorePlan plan;
    TpRetirementComposeShape compose = {&driver->layout, FIXTURE_PAIRS, driver->code_count, driver->prior_count};
    CHECK(driver_capacity(driver, &capacity));
    /* The plan refuses a malformed declaration, a prior count other than
     * the shape's, and a retained reservation beyond the store. */
    TpRetirementComposeRetained saved = driver->retained[4];
    driver->retained[4].kind = "Record";
    CHECK(!tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration, 3, 0, &plan));
    driver->retained[4] = saved;
    CHECK(reopen_store(driver));
    compose.prior_entries = driver->prior_count + 1;
    CHECK(!tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration, 3, 0, &plan));
    compose.prior_entries = driver->prior_count;
    CHECK(reopen_store(driver));
    driver->retained[5].files_max = TP_RETIREMENT_COMPOSE_GROUP_FILES;
    driver->retained[5].bytes_max = 1;
    CHECK(!tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration, 3, 0, &plan));
    driver->retained[5].files_max = 4;
    driver->retained[5].bytes_max = 4096;
    CHECK(reopen_store(driver));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration, 3, 0, &plan));
    /* Bound and retain are fixed once, before any publication. */
    CHECK(!tp_retirement_store_retain(&driver->store, digest_a));
    CHECK(reopen_store(driver));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration, 3, 0, &plan));
    CHECK(!tp_retirement_store_bound(&driver->store, 1));
    /* Settle only lowers the reservation, never below the published count,
     * and releases only upper-bounded slack: the exact kinds remain. */
    CHECK(reopen_store(driver));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration, 3, 0, &plan));
    unsigned planned = driver->store.planned_files, bounded = driver->store.bounded_files;
    CHECK(bounded && bounded < planned);
    CHECK(!tp_retirement_store_settle(&driver->store, planned + 1));
    CHECK(reopen_store(driver));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration, 3, 0, &plan));
    CHECK(!tp_retirement_store_settle(&driver->store, planned - bounded - 1));
    CHECK(reopen_store(driver));
    CHECK(tp_retirement_compose_plan(&driver->store, &capacity, &compose, &driver->declaration, 3, 0, &plan));
    CHECK(driver_import(driver, driver->untimed.files));
    CHECK(tp_retirement_store_settle(&driver->store, planned - bounded));
    CHECK(driver->store.bounded_files == 0 && driver->store.planned_files == planned - bounded);
    fixture_stop(&fixture);
}

BUSTER_GLOBAL_LOCAL int handoff_ready(Fixture* fixture)
{
    int valid = fixture_ready(fixture) && driver_compose(fixture->driver);
    return valid;
}

BUSTER_GLOBAL_LOCAL TpRetirementAuthorityState handoff_state(Driver* driver, int queue)
{
    TpRetirementAuthorityState state = tp_retirement_store_authority_state(driver->store_fd, queue, 7, 3,
        driver->digests[0], driver->result.context_sha256, &driver->authority);
    return state;
}

BUSTER_GLOBAL_LOCAL int handoff(Driver* driver, int queue, uint64_t job, uint64_t attempt, char const* context,
                                TpRetirementAuthorityJournal* journal)
{
    int valid = tp_retirement_store_authority_handoff(driver->store_fd, driver->authority_fd, queue, job, attempt,
                                                      driver->digests[0], context, &driver->authority, journal);
    return valid;
}

BUSTER_GLOBAL_LOCAL void test_handoff(void)
{
    Fixture fixture;
    CHECK(handoff_ready(&fixture));
    Driver* driver = fixture.driver;
    int queue = driver_directory(fixture.queue);
    TpRetirementAuthorityJournal journal;
    char const* context = driver->result.context_sha256;
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_ABSENT);
    /* The authenticated handoff's identities must be the authority's. */
    CHECK(!handoff(driver, queue, 8, 3, context, &journal));
    CHECK(!handoff(driver, queue, 7, 4, context, &journal));
    /* The pre-sample context is never the final authority context. */
    CHECK(!handoff(driver, queue, 7, 3, driver->result.raw_measurements_sha256, &journal));
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_ABSENT);
    CHECK(handoff(driver, queue, 7, 3, context, &journal));
    CHECK(!strcmp(journal.path, "authority-job-7-3.journal") && journal.bytes > 0);
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_COMPLETE);
    /* A retry never overwrites the sealed copy or journal. */
    struct stat before, after;
    CHECK(fstatat(queue, journal.path, &before, AT_SYMLINK_NOFOLLOW) == 0);
    CHECK(!handoff(driver, queue, 7, 3, context, &journal));
    CHECK(fstatat(queue, "authority-job-7-3.journal", &after, AT_SYMLINK_NOFOLLOW) == 0 &&
          after.st_ino == before.st_ino && after.st_size == before.st_size);
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_COMPLETE);
    /* The link/unlink crash window: a final name beside its `.pending`
     * temporary is a crash prefix, never complete. */
    CHECK(linkat(queue, "authority-job-7-3.journal", queue, "authority-job-7-3.journal.pending", 0) == 0);
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_INCOMPLETE);
    CHECK(unlinkat(queue, "authority-job-7-3.journal.pending", 0) == 0);
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_COMPLETE);
    /* Losing a retained A/A file after completion damages the handoff: both
     * names exist but the authority no longer reopens. */
    CHECK(unlinkat(driver->store_fd, "retirement-execution-aa-0000.jsonl", 0) == 0);
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_DAMAGED);
    close(queue);
    fixture_stop(&fixture);

    /* A removed sealed transcript shard damages it too. */
    CHECK(handoff_ready(&fixture));
    driver = fixture.driver;
    queue = driver_directory(fixture.queue);
    context = driver->result.context_sha256;
    CHECK(handoff(driver, queue, 7, 3, context, &journal));
    CHECK(unlinkat(driver->store_fd, "retirement-execution-ab-0000.jsonl", 0) == 0);
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_DAMAGED);
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
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_INCOMPLETE);
    CHECK(!handoff(driver, queue, 7, 3, context, &journal));
    CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_INCOMPLETE);
    close(queue);
    fixture_stop(&fixture);

    /* Failed journal syncs: at its pending entry (no final name) and after
     * its link (final and `.pending` both present). authority_copy syncs
     * four times (pending entry, file, link, unlink); the journal's syncs
     * follow in the same order. Neither prefix is ACKable. */
    for (unsigned failing = 5; failing <= 7; failing += 2)
    {
        CHECK(handoff_ready(&fixture));
        driver = fixture.driver;
        queue = driver_directory(fixture.queue);
        context = driver->result.context_sha256;
        tp_retirement_store_test_sync_calls = 0;
        tp_retirement_store_test_fail_sync = failing;
        CHECK(!handoff(driver, queue, 7, 3, context, &journal));
        tp_retirement_store_test_fail_sync = 0;
        CHECK(journal.path[0] == 0);
        struct stat pending;
        CHECK(fstatat(queue, "authority-job-7-3.journal.pending", &pending, AT_SYMLINK_NOFOLLOW) == 0);
        CHECK((fstatat(queue, "authority-job-7-3.journal", &pending, AT_SYMLINK_NOFOLLOW) == 0) == (failing == 7));
        CHECK(handoff_state(driver, queue) == TP_RETIREMENT_AUTHORITY_INCOMPLETE);
        close(queue);
        fixture_stop(&fixture);
    }

    /* No producer authority: nothing is copied or journalled. */
    CHECK(fixture_start(&fixture, FIXTURE_CLEAN));
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
    strcpy(forged.retained_sha256, driver->result.retained.sha256);
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
    else if (argc == 3 && !strcmp(argv[1], "canonical")) result = driver_canonical(argv[2], NULL);
    else if (argc == 4 && !strcmp(argv[1], "context")) result = driver_canonical(argv[2], argv[3]);
    else if (argc == 6 && !strcmp(argv[1], "retirement-replay") && !strcmp(argv[2], "--input") &&
             !strcmp(argv[4], "--output"))
        result = stub_adapter(argv[3], argv[5]);
    else
    {
        ssize_t length = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
        CHECK(length > 0 && (size_t)length < sizeof(self_path) - 1);
        if (length > 0) self_path[length] = 0;
        CHECK(fixture_self());
        test_canonical_json();
        test_compose_success();
        test_compose_series_shards();
        test_compose_refusals();
        test_production_capacity();
        test_budget_and_settle();
        test_handoff();
        fprintf(stderr, "COMPOSE_TEST assertions=%u failures=%u\n", assertions, failures);
        result = failures ? 1 : 0;
    }
    return result;
}
#endif
