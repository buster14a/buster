/* Original deterministic static Pages publisher. Source blobs and evidence
 * objects come from exact local Git revisions. No network, package manager,
 * template runtime or measurement-supplied command is executed.
 * Layout: bounded Git reads, resource qualification, catalog, generation seal.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#define main buster_performance_library_cli
#include "main.c"
#undef main
#include <signal.h>
#include <sys/wait.h>

static bool publish_revision(const char *revision)
{
    bool result = strlen(revision) == 40;
    bool nonzero = false;
    for (size_t i = 0; i < 40 && result; ++i)
    {
        result = (revision[i] >= '0' && revision[i] <= '9') || (revision[i] >= 'a' && revision[i] <= 'f');
        nonzero = nonzero || revision[i] != '0';
    }
    result = result && nonzero;
    return result;
}

static bool publish_git(const char *repository, const char *revision, PerfBytes path, PerfBuffer *output)
{
    char object[8192];
    int size = snprintf(object, sizeof(object), "%s:%.*s", revision, (int)path.size, path.data);
    int descriptors[2] = {-1, -1};
    bool result = publish_revision(revision) && perf_resource_path(path) && size > 0 && (size_t)size < sizeof(object) && pipe(descriptors) == 0;
    pid_t child = result ? fork() : -1;
    result = result && child >= 0;
    if (child == 0)
    {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) >= 0)
        {
            close(descriptors[1]);
            execlp("git", "git", "--no-pager", "-C", repository, "show", "--no-textconv", object, (char *)NULL);
        }
        _exit(126);
    }
    if (descriptors[1] >= 0)
    {
        close(descriptors[1]);
    }
    unsigned char block[65536];
    ssize_t count = 0;
    while (result && (count = read(descriptors[0], block, sizeof(block))) != 0)
    {
        if (count < 0)
        {
            result = errno == EINTR;
        }
        else
        {
            result = perf_buffer_append(output, (PerfBytes){.data = block, .size = (size_t)count});
        }
    }
    if (descriptors[0] >= 0)
    {
        close(descriptors[0]);
    }
    if (child > 0)
    {
        if (!result)
        {
            kill(child, SIGKILL);
        }
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        result = result && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    return result;
}

static bool publish_write(const char *root, const char *file, PerfBytes bytes)
{
    char path[4096];
    int size = snprintf(path, sizeof(path), "%s/%s", root, file);
    FILE *output = size > 0 && (size_t)size < sizeof(path) ? fopen(path, "wbx") : NULL;
    bool result = output != NULL;
    if (output != NULL)
    {
        result = fwrite(bytes.data, 1, bytes.size, output) == bytes.size;
        result = fclose(output) == 0 && result;
    }
    return result;
}

static bool publish_field(PerfBuffer *output, const char *name, PerfBytes value, bool *first)
{
    bool result = (*first || perf_buffer_byte(output, ',')) && perf_json_quote(output, perf_literal(name)) && perf_buffer_byte(output, ':') && perf_json_quote(output, value);
    *first = false;
    return result;
}

static bool publish_record(PerfBuffer *output, const char *path, PerfBytes bytes, bool *first)
{
    char digest[65];
    char size[32];
    snprintf(size, sizeof(size), "%zu", bytes.size);
    bool result = (*first || perf_buffer_byte(output, ',')) && perf_buffer_byte(output, '{');
    bool field = true;
    result = result && perf_sha256(bytes, digest) && publish_field(output, "path", perf_literal(path), &field) && publish_field(output, "sha256", perf_literal(digest), &field) && publish_field(output, "bytes", perf_literal(size), &field) && perf_buffer_byte(output, '}');
    *first = false;
    return result;
}

static bool publish_object(const char *repository, const char *revision, PerfBytes path, PerfBytes digest, PerfBytes size)
{
    PerfBuffer object = {0};
    char actual[65];
    uint64_t expected = 0;
    bool result = perf_uint64(size, &expected) && publish_git(repository, revision, path, &object) && object.size == expected && perf_sha256(perf_buffer_bytes(&object), actual) && perf_bytes_equal(perf_literal(actual), digest);
    free(object.data);
    return result;
}

static bool publish_dataset(const char *repository, const char *revision, PerfBytes path, const char *canonical, PerfBuffer *catalog, const PerfJson *index, uint32_t definition)
{
    PerfBuffer object = {0};
    char actual[65];
    char physical[8192];
    int size = snprintf(physical, sizeof(physical), "%s/%.*s", repository, (int)path.size, path.data == NULL ? (const unsigned char *)"" : path.data);
    PerfDataset dataset = {.evidence_root = repository};
    bool result = size > 0 && (size_t)size < sizeof(physical) && publish_git(repository, revision, path, &object) && perf_sha256(perf_buffer_bytes(&object), actual) && perf_dataset_load(&dataset, physical, canonical);
    PerfBuffer local = {0};
    result = result && perf_read_file(physical, &local) && perf_bytes_equal(perf_buffer_bytes(&local), perf_buffer_bytes(&object));
    uint32_t selected_table = perf_json_find(index, definition, "table");
    result = result && (selected_table == UINT32_MAX || perf_identity(&dataset.document, "tables", perf_json_text(index, selected_table)) != UINT32_MAX);
    for (size_t i = 0; i < dataset.resource_count && result; ++i)
    {
        const PerfJson *json = &dataset.document;
        uint32_t resource = dataset.resources[i].definition;
        PerfBytes relative = perf_json_text(json, perf_json_find(json, resource, "path"));
        PerfBuffer target = {0};
        if (relative.size != 0)
        {
            size_t directory = path.size;
            while (directory != 0 && path.data[directory - 1] != '/')
            {
                --directory;
            }
            result = perf_buffer_append(&target, (PerfBytes){.data = path.data, .size = directory}) && perf_buffer_append(&target, relative);
        }
        else
        {
            uint32_t source = perf_json_find(json, resource, "source");
            result = perf_buffer_append(&target, perf_json_text(json, perf_json_find(json, source, "path")));
        }
        result = result && publish_object(repository, revision, perf_buffer_bytes(&target), perf_json_text(json, perf_json_find(json, resource, "sha256")), perf_json_text(json, perf_json_find(json, resource, "bytes")));
        free(target.data);
    }
    if (result)
    {
        bool first = true;
        char url[8192];
        snprintf(url, sizeof(url), "https://raw.githubusercontent.com/buster14a/buster-performance/%s/%.*s", revision, (int)path.size, path.data);
        char bytes[32];
        snprintf(bytes, sizeof(bytes), "%zu", object.size);
        result = perf_buffer_byte(catalog, '{') && publish_field(catalog, "id", perf_json_text(&dataset.document, perf_json_find(&dataset.document, 0, "id")), &first) && publish_field(catalog, "manifest", perf_literal(url), &first) && publish_field(catalog, "sha256", perf_literal(actual), &first) && publish_field(catalog, "bytes", perf_literal(bytes), &first);
        const char *keys[] = {"name", "description", "table", "family"};
        for (size_t i = 0; i < 4 && result; ++i)
        {
            uint32_t value = perf_json_find(index, definition, keys[i]);
            if (value != UINT32_MAX)
            {
                result = index->nodes[value].kind == PERF_JSON_STRING && perf_json_text(index, value).size <= 1024 && publish_field(catalog, keys[i], perf_json_text(index, value), &first);
            }
        }
        result = result && perf_buffer_byte(catalog, '}');
    }
    perf_dataset_free(&dataset);
    free(local.data);
    free(object.data);
    return result;
}

int main(int argc, char **argv)
{
    bool result = argc == 8 && publish_revision(argv[2]) && publish_revision(argv[4]) && perf_resource_path(perf_literal(argv[5])) && perf_timestamp(perf_literal(argv[7]));
    char directory[4096] = {0};
    char canonical[4096] = {0};
    if (result)
    {
        int size = snprintf(directory, sizeof(directory), "%s/perf", argv[6]);
        int schema_size = snprintf(canonical, sizeof(canonical), "%s/buster-manifest-v1.schema.json", directory);
        result = size > 0 && (size_t)size < sizeof(directory) && schema_size > 0 && (size_t)schema_size < sizeof(canonical) && mkdir(argv[6], 0755) == 0 && mkdir(directory, 0755) == 0;
    }
    const char *files[] = {"index.html", "style.css", "app.mjs", "types.mjs", "arrow.mjs", "data.mjs", "charts.mjs", "trace.mjs", "trace-worker.mjs", "profile-view.mjs", "buster-manifest-v1.schema.json"};
    PerfBuffer seal = {0};
    bool seal_first = true;
    result = result && perf_buffer_append(&seal, perf_literal("{\"version\":1,\"status\":\"complete\",\"source_commit\":")) && perf_json_quote(&seal, perf_literal(argv[2])) && perf_buffer_append(&seal, perf_literal(",\"evidence_commit\":")) && perf_json_quote(&seal, perf_literal(argv[4])) && perf_buffer_append(&seal, perf_literal(",\"recorded_at\":")) && perf_json_quote(&seal, perf_literal(argv[7])) && perf_buffer_append(&seal, perf_literal(",\"files\":["));
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]) && result; ++i)
    {
        char source[512];
        snprintf(source, sizeof(source), i == 10 ? "docs/performance/%s" : "web/perf/%s", files[i]);
        PerfBuffer bytes = {0};
        result = publish_git(argv[1], argv[2], perf_literal(source), &bytes) && publish_write(directory, files[i], perf_buffer_bytes(&bytes)) && publish_record(&seal, files[i], perf_buffer_bytes(&bytes), &seal_first);
        free(bytes.data);
    }
    PerfBuffer index_bytes = {0};
    PerfJson index = {0};
    PerfBuffer catalog = {0};
    result = result && publish_git(argv[3], argv[4], perf_literal(argv[5]), &index_bytes) && perf_json_parse(&index, perf_buffer_bytes(&index_bytes));
    uint32_t datasets = perf_json_find(&index, 0, "datasets");
    result = result && datasets < index.node_count && index.nodes[datasets].kind == PERF_JSON_ARRAY && perf_json_count(&index, datasets) != 0 && perf_json_count(&index, datasets) <= 128;
    result = result && perf_buffer_append(&catalog, perf_literal("{\"version\":1,\"status\":\"complete\",\"source_commit\":")) && perf_json_quote(&catalog, perf_literal(argv[2])) && perf_buffer_append(&catalog, perf_literal(",\"evidence_commit\":")) && perf_json_quote(&catalog, perf_literal(argv[4])) && perf_buffer_append(&catalog, perf_literal(",\"recorded_at\":")) && perf_json_quote(&catalog, perf_literal(argv[7])) && perf_buffer_append(&catalog, perf_literal(",\"datasets\":["));
    bool first = true;
    for (uint32_t definition = perf_json_first(&index, datasets); definition != UINT32_MAX && result; definition = perf_json_after(&index, datasets, definition))
    {
        PerfBytes path = perf_json_text(&index, perf_json_find(&index, definition, "manifest"));
        result = (first || perf_buffer_byte(&catalog, ',')) && publish_dataset(argv[3], argv[4], path, canonical, &catalog, &index, definition);
        first = false;
    }
    PerfBytes census_path = perf_json_text(&index, perf_json_find(&index, 0, "census"));
    PerfBuffer census = {0};
    PerfJson census_document = {0};
    char digest[65];
    char url[8192];
    char census_size[32];
    result = result && publish_git(argv[3], argv[4], census_path, &census) && perf_sha256(perf_buffer_bytes(&census), digest) && perf_json_parse(&census_document, perf_buffer_bytes(&census)) && census_document.nodes[0].kind == PERF_JSON_ARRAY && perf_json_count(&census_document, 0) != 0 && perf_json_count(&census_document, 0) <= 4096;
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/buster14a/buster-performance/%s/%.*s", argc == 8 ? argv[4] : "", (int)census_path.size, census_path.data == NULL ? (const unsigned char *)"" : census_path.data);
    snprintf(census_size, sizeof(census_size), "%zu", census.size);
    bool census_first = true;
    result = result && perf_buffer_append(&catalog, perf_literal("],\"census\":{")) && publish_field(&catalog, "url", perf_literal(url), &census_first) && publish_field(&catalog, "sha256", perf_literal(digest), &census_first) && publish_field(&catalog, "bytes", perf_literal(census_size), &census_first) && perf_buffer_append(&catalog, perf_literal("}}\n")) && catalog.size <= 262144;
    PerfJson catalog_document = {0};
    result = result && perf_json_parse(&catalog_document, perf_buffer_bytes(&catalog));
    uint32_t catalog_datasets = perf_json_find(&catalog_document, 0, "datasets");
    for (uint32_t entry = perf_json_first(&catalog_document, catalog_datasets); entry != UINT32_MAX && result; entry = perf_json_after(&catalog_document, catalog_datasets, entry))
    {
        PerfBytes id = perf_json_text(&catalog_document, perf_json_find(&catalog_document, entry, "id"));
        result = perf_identifier(id) && perf_identity(&catalog_document, "datasets", id) == entry;
    }
    result = result && publish_write(directory, "catalog.json", perf_buffer_bytes(&catalog)) && publish_record(&seal, "catalog.json", perf_buffer_bytes(&catalog), &seal_first);
    result = result && perf_buffer_append(&seal, perf_literal("]}\n")) && publish_write(directory, "generation.json", perf_buffer_bytes(&seal)) && publish_write(argv[6], ".nojekyll", (PerfBytes){0});
    if (result)
    {
        result = perf_sha256(perf_buffer_bytes(&seal), digest);
        printf("{\"first_party\":true,\"sealed\":%s,\"catalog_bytes\":\"%zu\",\"generation_sha256\":\"%s\",\"source_commit\":\"%s\",\"evidence_commit\":\"%s\"}\n", result ? "true" : "false", catalog.size, digest, argv[2], argv[4]);
    }
    else
    {
        fputs("publish: generation not sealed; missing/corrupt/unreviewed source or evidence\nusage: publish SOURCE_REPO SOURCE_REV EVIDENCE_REPO EVIDENCE_REV INDEX_PATH NEW_OUTPUT_DIR RECORDED_AT\n", stderr);
    }
    free(seal.data);
    free(catalog.data);
    free(census.data);
    perf_json_free(&census_document);
    perf_json_free(&catalog_document);
    free(index_bytes.data);
    perf_json_free(&index);
    return result ? 0 : 1;
}
