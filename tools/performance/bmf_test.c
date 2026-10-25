/* Publication-contract probes against retained independent Arrow fixtures.
 * Mutations change one declared fact; rejected inputs must never seal a dataset.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#define main bmf_command_main
#include "bmf.c"
#undef main

static unsigned int bmf_test_count;
static unsigned int bmf_test_failures;

static void bmf_expect(bool result, const char *name)
{
    ++bmf_test_count;
    if (!result)
    {
        ++bmf_test_failures;
        fprintf(stderr, "FAIL: %s\n", name);
    }
}

static yyjson_doc *bmf_test_change(yyjson_doc *document, const char *pointer, const char *json)
{
    yyjson_mut_doc *changed = yyjson_doc_mut_copy(document, NULL);
    yyjson_doc *value = yyjson_read(json, strlen(json), 0);
    bool valid = changed != NULL && value != NULL;
    if (valid)
    {
        yyjson_mut_val *copy = yyjson_val_mut_copy(changed, yyjson_doc_get_root(value));
        valid = copy != NULL && yyjson_mut_doc_ptr_set(changed, pointer, copy);
    }
    yyjson_doc *result = valid ? yyjson_mut_doc_imut_copy(changed, NULL) : NULL;
    yyjson_doc_free(value);
    yyjson_mut_doc_free(changed);
    return result;
}

static bool bmf_test_dataset(yyjson_doc *document, yyjson_doc *schema, const char *root, const char *cache, uint64_t *rows, uint64_t *cells)
{
    BmfDataset dataset = {0};
    yyjson_mut_doc *copy = yyjson_doc_mut_copy(document, NULL);
    dataset.document = copy == NULL ? NULL : yyjson_mut_doc_imut_copy(copy, NULL);
    yyjson_mut_doc_free(copy);
    copy = yyjson_doc_mut_copy(schema, NULL);
    dataset.schema = copy == NULL ? NULL : yyjson_mut_doc_imut_copy(copy, NULL);
    yyjson_mut_doc_free(copy);
    char resolved[PATH_MAX];
    bool root_valid = realpath(root, resolved) != NULL;
    int root_size = root_valid ? snprintf(dataset.root, sizeof(dataset.root), "%s/", resolved) : -1;
    dataset.cache = cache;
    bool result = root_size > 0 && (size_t)root_size < sizeof(dataset.root) && dataset.document != NULL && dataset.schema != NULL;
    yyjson_val *data = yyjson_doc_get_root(dataset.document);
    result = result && bmf_schema_check(yyjson_doc_get_root(dataset.schema), data) && bmf_metadata_check(data) && bmf_resources_load(&dataset) && bmf_tables_load(&dataset) && bmf_relationships_check(&dataset);
    *rows = dataset.total_rows;
    *cells = dataset.compared_cells;
    bmf_dataset_release(&dataset);
    return result;
}

static bool bmf_test_cache(yyjson_doc *manifest, const char *archive, const char *cache)
{
    bool result = mkdir(cache, 0700) == 0 || errno == EEXIST;
    size_t index, length;
    yyjson_val *resource;
    yyjson_arr_foreach(bmf_get(yyjson_doc_get_root(manifest), "resources"), index, length, resource)
    {
        if (bmf_get(resource, "url") != NULL && result)
        {
            char input[PATH_MAX];
            char output[PATH_MAX];
            int size = snprintf(input, sizeof(input), "%s/%s", archive, bmf_string(bmf_get(bmf_get(resource, "source"), "path")));
            result = size > 0 && (size_t)size < sizeof(input);
            size = snprintf(output, sizeof(output), "%s/%s", cache, bmf_string(bmf_get(resource, "sha256")));
            result = result && size > 0 && (size_t)size < sizeof(output);
            BmfBuffer bytes = {0};
            result = result && bmf_file_read(input, &bytes);
            FILE *file = result ? fopen(output, "wb") : NULL;
            result = result && file != NULL;
            if (file != NULL)
            {
                result = fwrite(bytes.data, 1, bytes.size, file) == bytes.size && result;
                result = fclose(file) == 0 && result;
            }
            free(bytes.data);
        }
    }
    return result;
}

typedef struct BmfMutation BmfMutation;
struct BmfMutation
{
    const char *name;
    const char *pointer;
    const char *json;
    bool accepted;
    bool foreign;
};

int main(int argc, char **argv)
{
    bool ready = argc == 2;
    const char *archive = ready ? argv[1] : "";
    char foreign_root[PATH_MAX];
    char custom_root[PATH_MAX];
    char path[PATH_MAX];
    char cache[] = "build/performance/test-cache-XXXXXX";
    snprintf(foreign_root, sizeof(foreign_root), "%s/docs/measurements/performance-format-2026-10-24/foreign-strings-example", archive);
    snprintf(custom_root, sizeof(custom_root), "%s/docs/measurements/performance-format-2026-10-24/profile-example", archive);
    snprintf(path, sizeof(path), "%s/manifest.bmf.json", foreign_root);
    yyjson_doc *foreign = ready ? bmf_json_file(path) : NULL;
    snprintf(path, sizeof(path), "%s/manifest.bmf.json", custom_root);
    yyjson_doc *custom = ready ? bmf_json_file(path) : NULL;
    yyjson_doc *schema = bmf_json_file("docs/performance/buster-manifest-v1.schema.json");
    ready = ready && foreign != NULL && custom != NULL && schema != NULL && mkdtemp(cache) != NULL && bmf_test_cache(foreign, archive, cache);
    bmf_expect(ready, "fixture setup");
    uint64_t rows = 0;
    uint64_t cells = 0;
    if (ready)
    {
        bmf_expect(bmf_test_dataset(foreign, schema, foreign_root, cache, &rows, &cells) && rows == 300 && cells == 7308, "all 300 foreign rows / 7308 source cells");
        bmf_expect(bmf_test_dataset(custom, schema, custom_root, cache, &rows, &cells) && rows == 2 && cells == 12, "all two arbitrary rows / 12 exact cells");
        const BmfMutation cases[] = {
            {.name = "unknown version", .pointer = "/version", .json = "2"},
            {.name = "required capability", .pointer = "/requires/0", .json = "\"future.codec\""},
            {.name = "optional metadata retained", .pointer = "/extensions", .json = "{\"future\":{\"nested\":[1,2,\"18446744073709551615\"]}}", .accepted = true},
            {.name = "new metric quantity", .pointer = "/metrics/0/quantity", .json = "\"unseen.energy-quality\"", .accepted = true},
            {.name = "new unit dimension", .pointer = "/metrics/0/unit/dimensions", .json = "{\"widget\":-2}", .accepted = true},
            {.name = "partial stays partial", .pointer = "/status", .json = "\"partial\"", .accepted = true},
            {.name = "aborted stays aborted", .pointer = "/status", .json = "\"aborted\"", .accepted = true},
            {.name = "bad date", .pointer = "/recorded_at", .json = "\"2026-02-30T12:00:00Z\""},
            {.name = "bad timezone", .pointer = "/recorded_at", .json = "\"2026-10-24T12:00:00+25:00\""},
            {.name = "duplicate resource id", .pointer = "/resources/1/id", .json = "\"arbitrary-custom-arrow\""},
            {.name = "trailing newline identity", .pointer = "/resources/0/id", .json = "\"arbitrary-custom-arrow\\n\""},
            {.name = "unsafe traversal", .pointer = "/resources/0/path", .json = "\"../capture.pftrace\""},
            {.name = "absent resource", .pointer = "/resources/0/path", .json = "\"absent.arrow\""},
            {.name = "wrong byte count", .pointer = "/resources/0/bytes", .json = "\"1\""},
            {.name = "uint64 byte overflow", .pointer = "/resources/0/bytes", .json = "\"18446744073709551616\""},
            {.name = "corrupt hash", .pointer = "/resources/0/sha256", .json = "\"0000000000000000000000000000000000000000000000000000000000000000\""},
            {.name = "wrong Arrow rows", .pointer = "/resources/0/rows", .json = "\"3\""},
            {.name = "wrong JSONL rows", .pointer = "/resources/1/rows", .json = "\"3\""},
            {.name = "schema sidecar has rows", .pointer = "/tables/0/schema", .json = "\"arbitrary-custom-arrow\""},
            {.name = "partition read twice", .pointer = "/tables/0/data", .json = "[\"arbitrary-custom-arrow\",\"arbitrary-custom-arrow\"]"},
            {.name = "JSONL primary", .pointer = "/tables/0/data", .json = "[\"arbitrary-custom-jsonl\"]", .accepted = true},
            {.name = "unknown metric ref", .pointer = "/tables/0/measurements/penalty/metric_id", .json = "\"absent\""},
            {.name = "exact unknown field", .pointer = "/tables/0/dimensions", .json = "[\"未知 field\"]"},
            {.name = "unknown status field", .pointer = "/tables/0/measurements/penalty/status_column", .json = "\"absent\""},
            {.name = "negative sample count", .pointer = "/tables/0/measurements/penalty/sample_count", .json = "{\"constant\":-1}"},
            {.name = "unsafe sample count", .pointer = "/tables/0/measurements/penalty/sample_count", .json = "{\"constant\":9007199254740992}"},
            {.name = "exact sample count", .pointer = "/tables/0/measurements/penalty/sample_count", .json = "{\"constant\":\"18446744073709551615\"}", .accepted = true},
            {.name = "trace execution absent", .pointer = "/traces/0/execution_id", .json = "\"absent\""},
            {.name = "trace uses table resource", .pointer = "/traces/0/resource", .json = "\"arbitrary-custom-arrow\""},
            {.name = "unresolved trace source", .pointer = "/traces/0/source_artifacts", .json = "[\"absent\"]"},
            {.name = "duplicate primary key", .pointer = "/tables/0/primary_key", .json = "[\"seq\"]", .foreign = true},
            {.name = "missing foreign target", .pointer = "/tables/0/foreign_keys/0/target_table", .json = "\"absent\"", .foreign = true},
            {.name = "foreign arity mismatch", .pointer = "/tables/0/foreign_keys/0/target_columns", .json = "[\"sequence\",\"compiler\"]", .foreign = true},
            {.name = "foreign type mismatch", .pointer = "/tables/0/foreign_keys/0/target_columns", .json = "[\"compiler\"]", .foreign = true},
            {.name = "wrong build context", .pointer = "/tables/0/bindings/build_context", .json = "\"protocol\"", .foreign = true},
            {.name = "unknown scope", .pointer = "/tables/0/measurements/wall_ns/scope_id", .json = "\"absent\"", .foreign = true}
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            const BmfMutation *test = &cases[i];
            yyjson_doc *changed = bmf_test_change(test->foreign ? foreign : custom, test->pointer, test->json);
            bool accepted = changed != NULL && bmf_test_dataset(changed, schema, test->foreign ? foreign_root : custom_root, cache, &rows, &cells);
            bmf_expect(changed != NULL && accepted == test->accepted, test->name);
            yyjson_doc_free(changed);
        }
        BmfResource physical = {0};
        snprintf(path, sizeof(path), "%s/arbitrary-custom.schema.arrow", custom_root);
        bmf_expect(bmf_file_read(path, &physical.bytes), "read independent Arrow schema");
        struct ArrowSchema physical_schema = {0};
        bool mapped = true;
        bmf_expect(bmf_arrow_read(&physical, &physical_schema, NULL, NULL, &rows, &mapped), "nanoarrow reads Arrow JS schema");
        snprintf(path, sizeof(path), "%s/arbitrary-custom.jsonl", custom_root);
        BmfBuffer jsonl = {0};
        bmf_expect(bmf_file_read(path, &jsonl), "read JSONL");
        unsigned char *newline = memchr(jsonl.data, '\n', jsonl.size);
        yyjson_doc *row = newline == NULL ? NULL : bmf_json_read(jsonl.data, (size_t)(newline - jsonl.data));
        bmf_expect(row != NULL && bmf_json_typed(&physical_schema, yyjson_doc_get_root(row)), "valid exact uint64 and decimal");
        const BmfMutation typed_cases[] = {
            {.name = "uint64 overflow", .pointer = "/digest", .json = "\"18446744073709551616\""},
            {.name = "uint64 ingested as Number", .pointer = "/digest", .json = "18446744073709551615"},
            {.name = "decimal rounds", .pointer = "/penalty", .json = "\"1.2500001\""},
            {.name = "equivalent decimal spelling", .pointer = "/penalty", .json = "\"1.25\"", .accepted = true},
            {.name = "negative uint64", .pointer = "/items", .json = "\"-1\""},
            {.name = "legitimate zero", .pointer = "/items", .json = "\"0\"", .accepted = true},
            {.name = "null nonnullable digest", .pointer = "/digest", .json = "null"},
            {.name = "decimal JSON Number", .pointer = "/penalty", .json = "1.25"}
        };
        for (size_t i = 0; i < sizeof(typed_cases) / sizeof(typed_cases[0]); ++i)
        {
            const BmfMutation *test = &typed_cases[i];
            yyjson_doc *changed = bmf_test_change(row, test->pointer, test->json);
            bool accepted = changed != NULL && bmf_json_typed(&physical_schema, yyjson_doc_get_root(changed));
            bmf_expect(changed != NULL && accepted == test->accepted, test->name);
            yyjson_doc_free(changed);
        }
        const unsigned char duplicate[] = "{\"a\":1,\"a\":2}";
        yyjson_doc *bad = bmf_json_read(duplicate, sizeof(duplicate) - 1);
        bmf_expect(bad == NULL, "duplicate JSON fields");
        yyjson_doc_free(bad);
        bmf_expect(!bmf_https_url("https://user:secret@example.com/file"), "URL credentials");
        bmf_expect(!bmf_https_url("file:///etc/passwd"), "URL protocol");
        bmf_expect(!bmf_https_url("https://example.com/file#fragment"), "URL fragment");
        bmf_expect(bmf_timestamp("2024-02-29T23:59:59.123+02:00"), "leap timestamp");
        yyjson_doc_free(row);
        free(jsonl.data);
        ArrowSchemaRelease(&physical_schema);
        free(physical.bytes.data);
    }
    yyjson_doc_free(foreign);
    yyjson_doc_free(custom);
    yyjson_doc_free(schema);
    curl_global_cleanup();
    printf("{\"checks\":%u,\"failures\":%u}\n", bmf_test_count, bmf_test_failures);
    return bmf_test_failures == 0 ? 0 : 1;
}
