/* Buster's offline publication validator. Arrow owns physical storage; this
 * tool checks benchmark metadata, immutable resources and table relationships.
 * All input walks use explicit work lists. Nothing in a manifest is executed.
 * bmf_json.c: strict JSON/schema; bmf_resources.c: bytes; bmf_tables.c: Arrow.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <curl/curl.h>
#include <openssl/evp.h>
#include "yyjson.h"
#include "nanoarrow/nanoarrow.h"
#include "nanoarrow/nanoarrow_ipc.h"

#define BUSTER_BMF_BYTES (64u * 1024u * 1024u)
#define BUSTER_BMF_TASKS 262144u
#define BUSTER_BMF_DEPTH 128u

typedef struct BmfBuffer BmfBuffer;
struct BmfBuffer
{
    unsigned char *data;
    size_t size;
};

typedef struct BmfResource BmfResource;
struct BmfResource
{
    yyjson_val *definition;
    BmfBuffer bytes;
};

typedef struct BmfTable BmfTable;
struct BmfTable
{
    yyjson_val *definition;
    struct ArrowSchema schema;
    yyjson_doc *rows;
    uint64_t row_count;
    bool json_mapping;
};

typedef struct BmfDataset BmfDataset;
struct BmfDataset
{
    yyjson_doc *document;
    yyjson_doc *schema;
    BmfResource *resources;
    size_t resource_count;
    BmfTable *tables;
    size_t table_count;
    uint64_t compared_cells;
    uint64_t total_rows;
    char root[PATH_MAX];
    const char *cache;
    bool fetch_https;
};

static const char *bmf_string(yyjson_val *value)
{
    const char *result = yyjson_get_str(value);
    return result == NULL ? "" : result;
}

static yyjson_val *bmf_get(yyjson_val *value, const char *key)
{
    yyjson_val *result = yyjson_obj_get(value, key);
    return result;
}

static bool bmf_error(const char *message, const char *detail)
{
    fprintf(stderr, "bmf: %s: %s\n", message, detail == NULL ? "" : detail);
    return false;
}

static bool bmf_buffer_append(BmfBuffer *buffer, const void *bytes, size_t size)
{
    bool result = size <= BUSTER_BMF_BYTES - buffer->size;
    if (result && size != 0)
    {
        unsigned char *grown = realloc(buffer->data, buffer->size + size + 1);
        result = grown != NULL;
        if (result)
        {
            buffer->data = grown;
            memcpy(buffer->data + buffer->size, bytes, size);
            buffer->size += size;
            buffer->data[buffer->size] = 0;
        }
    }
    return result;
}

static bool bmf_file_read(const char *path, BmfBuffer *output)
{
    FILE *file = fopen(path, "rb");
    bool result = file != NULL;
    if (result)
    {
        struct stat info;
        result = fstat(fileno(file), &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 && info.st_size <= BUSTER_BMF_BYTES;
        unsigned char block[65536];
        size_t size = 0;
        while (result && (size = fread(block, 1, sizeof(block), file)) != 0)
        {
            result = bmf_buffer_append(output, block, size);
        }
        result = !ferror(file) && result;
        result = fclose(file) == 0 && result;
    }
    if (!result)
    {
        bmf_error("cannot read bounded regular file", path);
    }
    return result;
}

static bool bmf_sha256(const void *bytes, size_t size, char output[65])
{
    unsigned char digest[32];
    unsigned int digest_size = 0;
    bool result = EVP_Digest(bytes, size, digest, &digest_size, EVP_sha256(), NULL) == 1 && digest_size == 32;
    if (result)
    {
        static const char digits[] = "0123456789abcdef";
        for (size_t i = 0; i < 32; ++i)
        {
            output[2 * i] = digits[digest[i] >> 4];
            output[2 * i + 1] = digits[digest[i] & 15];
        }
        output[64] = 0;
    }
    return result;
}

#include "bmf_json.c"
#include "bmf_resources.c"
#include "bmf_tables.c"

static void bmf_dataset_release(BmfDataset *dataset)
{
    for (size_t i = 0; i < dataset->resource_count; ++i)
    {
        free(dataset->resources[i].bytes.data);
    }
    for (size_t i = 0; i < dataset->table_count; ++i)
    {
        BmfTable *table = &dataset->tables[i];
        if (table->schema.release != NULL)
        {
            ArrowSchemaRelease(&table->schema);
        }
        yyjson_doc_free(table->rows);
    }
    free(dataset->resources);
    free(dataset->tables);
    yyjson_doc_free(dataset->document);
    yyjson_doc_free(dataset->schema);
}

int main(int argc, char **argv)
{
    BmfDataset dataset = {0};
    const char *manifest = NULL;
    const char *schema = "docs/performance/buster-manifest-v1.schema.json";
    bool valid = true;
    for (int i = 1; i < argc && valid; ++i)
    {
        if (strcmp(argv[i], "--schema") == 0 && i + 1 < argc)
        {
            schema = argv[++i];
        }
        else if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc)
        {
            dataset.cache = argv[++i];
        }
        else if (strcmp(argv[i], "--fetch-https") == 0)
        {
            dataset.fetch_https = true;
        }
        else if (manifest == NULL && argv[i][0] != '-')
        {
            manifest = argv[i];
        }
        else
        {
            valid = false;
        }
    }
    if (!valid || manifest == NULL)
    {
        fprintf(stderr, "usage: bmf [--schema FILE] [--cache DIR] [--fetch-https] manifest.bmf.json\n");
        valid = false;
    }
    char resolved[PATH_MAX];
    if (valid)
    {
        valid = realpath(manifest, resolved) != NULL;
        if (valid)
        {
            strcpy(dataset.root, resolved);
            char *slash = strrchr(dataset.root, '/');
            slash[1] = 0;
            dataset.document = bmf_json_file(resolved);
            dataset.schema = bmf_json_file(schema);
            valid = dataset.document != NULL && dataset.schema != NULL;
        }
        else
        {
            bmf_error("cannot resolve manifest", manifest);
        }
    }
    if (valid)
    {
        yyjson_val *root = yyjson_doc_get_root(dataset.document);
        valid = bmf_schema_check(yyjson_doc_get_root(dataset.schema), root) && bmf_metadata_check(root);
    }
    if (valid)
    {
        valid = bmf_resources_load(&dataset) && bmf_tables_load(&dataset) && bmf_relationships_check(&dataset);
    }
    if (valid)
    {
        printf("{\"validator\":\"buster-bmf-1\",\"valid\":true,\"resources\":%zu,\"tables\":%zu,\"rows\":\"%" PRIu64 "\",\"typed_cells_compared\":\"%" PRIu64 "\",\"status\":\"%s\"}\n", dataset.resource_count, dataset.table_count, dataset.total_rows, dataset.compared_cells, bmf_string(bmf_get(yyjson_doc_get_root(dataset.document), "status")));
    }
    bmf_dataset_release(&dataset);
    curl_global_cleanup();
    return valid ? 0 : 1;
}
