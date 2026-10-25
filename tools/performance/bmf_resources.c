/* Resource content verification. Network reads are opt-in; local symlinks must
 * resolve inside the dataset. The cache is addressed by expected SHA-256.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
static size_t bmf_curl_write(char *bytes, size_t width, size_t count, void *user)
{
    BmfBuffer *buffer = user;
    size_t size = width != 0 && count <= SIZE_MAX / width ? width * count : 0;
    size_t result = size != 0 && bmf_buffer_append(buffer, bytes, size) ? size : 0;
    return result;
}

static bool bmf_https_url(const char *url)
{
    CURLU *parsed = curl_url();
    char *scheme = NULL;
    char *host = NULL;
    char *user = NULL;
    char *fragment = NULL;
    bool result = parsed != NULL && curl_url_set(parsed, CURLUPART_URL, url, 0) == CURLUE_OK;
    if (result)
    {
        result = curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK && strcmp(scheme, "https") == 0;
        result = result && curl_url_get(parsed, CURLUPART_HOST, &host, 0) == CURLUE_OK && host[0] != 0;
        result = result && curl_url_get(parsed, CURLUPART_USER, &user, 0) == CURLUE_NO_USER;
        result = result && curl_url_get(parsed, CURLUPART_FRAGMENT, &fragment, 0) == CURLUE_NO_FRAGMENT;
    }
    curl_free(scheme);
    curl_free(host);
    curl_free(user);
    curl_free(fragment);
    curl_url_cleanup(parsed);
    return result;
}

static bool bmf_resource_remote(BmfDataset *dataset, yyjson_val *definition, BmfBuffer *bytes)
{
    const char *url = bmf_string(bmf_get(definition, "url"));
    const char *digest = bmf_string(bmf_get(definition, "sha256"));
    bool result = bmf_https_url(url);
    bool cached = false;
    char path[PATH_MAX];
    if (result && dataset->cache != NULL)
    {
        int size = snprintf(path, sizeof(path), "%s/%s", dataset->cache, digest);
        result = size > 0 && (size_t)size < sizeof(path);
        if (result && access(path, F_OK) == 0)
        {
            result = bmf_file_read(path, bytes);
            cached = result;
        }
    }
    if (result && !cached)
    {
        result = dataset->fetch_https;
        if (result)
        {
            CURL *request = curl_easy_init();
            result = request != NULL;
            if (result)
            {
                curl_easy_setopt(request, CURLOPT_URL, url);
                curl_easy_setopt(request, CURLOPT_PROTOCOLS_STR, "https");
                curl_easy_setopt(request, CURLOPT_REDIR_PROTOCOLS_STR, "https");
                curl_easy_setopt(request, CURLOPT_FOLLOWLOCATION, 0L);
                curl_easy_setopt(request, CURLOPT_FAILONERROR, 1L);
                curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT, 15L);
                curl_easy_setopt(request, CURLOPT_TIMEOUT, 90L);
                curl_easy_setopt(request, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)BUSTER_BMF_BYTES);
                curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, bmf_curl_write);
                curl_easy_setopt(request, CURLOPT_WRITEDATA, bytes);
                curl_easy_setopt(request, CURLOPT_USERAGENT, "buster-bmf/1");
                long status = 0;
                result = curl_easy_perform(request) == CURLE_OK;
                result = curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK && result && status == 200;
                curl_easy_cleanup(request);
            }
        }
        else
        {
            bmf_error("remote resource needs a digest cache or --fetch-https", url);
        }
    }
    return result;
}

static bool bmf_resources_load(BmfDataset *dataset)
{
    yyjson_val *definitions = bmf_get(yyjson_doc_get_root(dataset->document), "resources");
    dataset->resource_count = yyjson_arr_size(definitions);
    dataset->resources = calloc(dataset->resource_count + 1, sizeof(*dataset->resources));
    bool result = dataset->resources != NULL && curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    uint64_t aggregate = 0;
    for (size_t i = 0; i < dataset->resource_count && result; ++i)
    {
        BmfResource *resource = &dataset->resources[i];
        resource->definition = yyjson_arr_get(definitions, i);
        yyjson_val *path_value = bmf_get(resource->definition, "path");
        uint64_t expected_size = 0;
        result = bmf_uint_string(bmf_get(resource->definition, "bytes"), &expected_size) && expected_size <= BUSTER_BMF_BYTES;
        aggregate += expected_size;
        result = result && aggregate <= 8u * BUSTER_BMF_BYTES;
        if (result && path_value != NULL)
        {
            const char *path = bmf_string(path_value);
            result = bmf_regex("^[A-Za-z0-9][A-Za-z0-9._/-]*\\z", path) && path[0] != '/' && strstr(path, "//") == NULL && !bmf_regex("(^|/)\\.\\.?(/|$)", path);
            char joined[PATH_MAX];
            char resolved[PATH_MAX];
            int size = snprintf(joined, sizeof(joined), "%s%s", dataset->root, path);
            result = result && size > 0 && (size_t)size < sizeof(joined);
            result = result && realpath(joined, resolved) != NULL;
            result = result && strncmp(resolved, dataset->root, strlen(dataset->root)) == 0;
            result = result && bmf_file_read(resolved, &resource->bytes);
        }
        else if (result)
        {
            result = bmf_resource_remote(dataset, resource->definition, &resource->bytes);
        }
        char digest[65];
        result = result && resource->bytes.size == expected_size && bmf_sha256(resource->bytes.data, resource->bytes.size, digest);
        result = result && strcmp(digest, bmf_string(bmf_get(resource->definition, "sha256"))) == 0;
        if (!result)
        {
            bmf_error("missing, unsafe, oversized or corrupt resource", bmf_string(bmf_get(resource->definition, "id")));
        }
    }
    return result;
}

static BmfResource *bmf_resource_find(BmfDataset *dataset, const char *id)
{
    BmfResource *result = NULL;
    for (size_t i = 0; i < dataset->resource_count; ++i)
    {
        if (yyjson_equals_str(bmf_get(dataset->resources[i].definition, "id"), id))
        {
            result = &dataset->resources[i];
        }
    }
    return result;
}
