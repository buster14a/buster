/* First-party offline publication checks. No network client is required:
 * immutable URL resources resolve from a digest cache or an explicit evidence
 * root and are checked against their declared bytes and SHA-256.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#define BUSTER_PERF_RESOURCE_LIMIT 4096u
#define BUSTER_PERF_AGGREGATE_LIMIT (256u * 1024u * 1024u)

typedef struct PerfResource PerfResource;
struct PerfResource
{
    uint32_t definition;
    PerfBuffer bytes;
    PerfArrowInfo arrow;
    uint64_t rows;
};

typedef struct PerfTable PerfTable;
struct PerfTable
{
    uint32_t definition;
    PerfArrowSchema schema;
    PerfBuffer rows;
    uint32_t *offsets;
    uint64_t count;
    bool json_mapping;
};

typedef struct PerfDataset PerfDataset;
struct PerfDataset
{
    PerfJson document;
    PerfResource *resources;
    PerfTable *tables;
    size_t resource_count;
    size_t table_count;
    uint64_t rows;
    uint64_t cells_compared;
    uint64_t loaded_bytes;
    size_t native_only_tables;
    char root[4096];
    const char *cache;
    const char *evidence_root;
};

static bool perf_error(const char *message, PerfBytes detail)
{
    fprintf(stderr, "bmf: %s: %.*s\n", message, (int)(detail.size < 256 ? detail.size : 256), detail.data == NULL ? "" : (const char *)detail.data);
    return false;
}

static uint32_t perf_json_first(const PerfJson *json, uint32_t parent)
{
    uint32_t result = parent < json->node_count && (json->nodes[parent].kind == PERF_JSON_ARRAY || json->nodes[parent].kind == PERF_JSON_OBJECT) && json->nodes[parent].children != 0 ? parent + 1 : UINT32_MAX;
    return result;
}

static uint32_t perf_json_after(const PerfJson *json, uint32_t parent, uint32_t item)
{
    uint32_t result = json->nodes[parent].kind == PERF_JSON_OBJECT ? json->nodes[item + 1].next : json->nodes[item].next;
    result = result < json->nodes[parent].next ? result : UINT32_MAX;
    return result;
}

static size_t perf_json_count(const PerfJson *json, uint32_t node)
{
    size_t result = node < json->node_count ? json->nodes[node].children : 0;
    return result;
}

static uint32_t perf_identity(const PerfJson *json, const char *collection, PerfBytes id)
{
    uint32_t array = perf_json_find(json, 0, collection);
    uint32_t result = UINT32_MAX;
    for (uint32_t value = perf_json_first(json, array); value != UINT32_MAX && result == UINT32_MAX; value = perf_json_after(json, array, value))
    {
        if (perf_bytes_equal(perf_json_text(json, perf_json_find(json, value, "id")), id))
        {
            result = value;
        }
    }
    return result;
}

static bool perf_metadata(const PerfJson *json)
{
    bool result = true;
    const char *collections[] = {"resources", "tables", "metrics", "scopes", "instruments", "contexts", "traces"};
    for (size_t c = 0; c < sizeof(collections) / sizeof(collections[0]) && result; ++c)
    {
        uint32_t array = perf_json_find(json, 0, collections[c]);
        result = perf_json_count(json, array) <= BUSTER_PERF_RESOURCE_LIMIT;
        for (uint32_t value = perf_json_first(json, array); value != UINT32_MAX && result; value = perf_json_after(json, array, value))
        {
            PerfBytes id = perf_json_text(json, perf_json_find(json, value, "id"));
            result = perf_identifier(id) && perf_identity(json, collections[c], id) == value;
        }
    }
    uint32_t requires = perf_json_find(json, 0, "requires");
    for (uint32_t value = perf_json_first(json, requires); value != UINT32_MAX && result; value = perf_json_after(json, requires, value))
    {
        result = perf_json_is(json, value, "arrow-ipc") || perf_json_is(json, value, "jsonl.arrow-mapping.v1") || perf_json_is(json, value, "perfetto.protobuf");
    }
    uint32_t contexts = perf_json_find(json, 0, "contexts");
    for (uint32_t value = perf_json_first(json, contexts); value != UINT32_MAX && result; value = perf_json_after(json, contexts, value))
    {
        uint32_t evidence = perf_json_find(json, value, "evidence");
        for (uint32_t item = perf_json_first(json, evidence); item != UINT32_MAX && result; item = perf_json_after(json, evidence, item))
        {
            result = perf_identity(json, "resources", perf_json_text(json, item)) != UINT32_MAX;
        }
    }
    uint32_t traces = perf_json_find(json, 0, "traces");
    for (uint32_t value = perf_json_first(json, traces); value != UINT32_MAX && result; value = perf_json_after(json, traces, value))
    {
        uint32_t resource = perf_identity(json, "resources", perf_json_text(json, perf_json_find(json, value, "resource")));
        uint32_t context = perf_identity(json, "contexts", perf_json_text(json, perf_json_find(json, value, "execution_id")));
        result = perf_json_is(json, perf_json_find(json, resource, "format"), "perfetto-protobuf") && perf_json_is(json, perf_json_find(json, context, "kind"), "execution");
        uint32_t sources = perf_json_find(json, value, "source_artifacts");
        for (uint32_t item = perf_json_first(json, sources); item != UINT32_MAX && result; item = perf_json_after(json, sources, item))
        {
            result = perf_identity(json, "resources", perf_json_text(json, item)) != UINT32_MAX;
        }
    }
    result = result || perf_error("duplicate/unresolved ID or unsupported required capability", (PerfBytes){0});
    return result;
}

static bool perf_read_below(const char *root, PerfBytes path, PerfBuffer *output)
{
    bool result = root != NULL && perf_resource_path(path) && path.size < 4096;
    int directory = result ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    result = result && directory >= 0;
    size_t cursor = 0;
    while (cursor < path.size && result)
    {
        size_t start = cursor;
        while (cursor < path.size && path.data[cursor] != '/')
        {
            ++cursor;
        }
        char component[4096];
        memcpy(component, path.data + start, cursor - start);
        component[cursor - start] = 0;
        bool last = cursor == path.size;
        int descriptor = openat(directory, component, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | (last ? 0 : O_DIRECTORY));
        result = descriptor >= 0;
        if (last && result)
        {
            struct stat information;
            result = fstat(descriptor, &information) == 0 && S_ISREG(information.st_mode) && information.st_size >= 0 && information.st_size <= BUSTER_PERF_BYTE_LIMIT;
            unsigned char block[65536];
            ssize_t size = 0;
            while (result && (size = read(descriptor, block, sizeof(block))) > 0)
            {
                result = perf_buffer_append(output, (PerfBytes){.data = block, .size = (size_t)size});
            }
            result = size == 0 && result;
            close(descriptor);
        }
        else if (result)
        {
            close(directory);
            directory = descriptor;
        }
        cursor += !last;
    }
    if (directory >= 0)
    {
        close(directory);
    }
    return result;
}

static bool perf_jsonl_count(PerfBytes bytes, uint64_t *count)
{
    size_t cursor = 0;
    bool result = true;
    while (cursor < bytes.size && result)
    {
        size_t start = cursor;
        bool blank = true;
        while (cursor < bytes.size && bytes.data[cursor] != '\n')
        {
            blank = blank && (bytes.data[cursor] == ' ' || bytes.data[cursor] == '\t' || bytes.data[cursor] == '\r');
            ++cursor;
        }
        if (!blank)
        {
            PerfJson row = {0};
            result = *count < BUSTER_PERF_ROW_LIMIT && perf_json_parse(&row, (PerfBytes){.data = bytes.data + start, .size = cursor - start}) && row.nodes[0].kind == PERF_JSON_OBJECT;
            ++*count;
            perf_json_free(&row);
        }
        cursor += cursor < bytes.size;
    }
    return result;
}

static bool perf_resources_load(PerfDataset *dataset)
{
    const PerfJson *json = &dataset->document;
    uint32_t definitions = perf_json_find(json, 0, "resources");
    dataset->resource_count = perf_json_count(json, definitions);
    dataset->resources = calloc(dataset->resource_count + 1, sizeof(*dataset->resources));
    bool result = dataset->resources != NULL;
    size_t index = 0;
    for (uint32_t definition = perf_json_first(json, definitions); definition != UINT32_MAX && result; definition = perf_json_after(json, definitions, definition))
    {
        PerfResource *resource = &dataset->resources[index++];
        resource->definition = definition;
        uint64_t expected = 0;
        PerfBytes digest = perf_json_text(json, perf_json_find(json, definition, "sha256"));
        result = perf_uint64(perf_json_text(json, perf_json_find(json, definition, "bytes")), &expected) && expected <= BUSTER_PERF_BYTE_LIMIT && dataset->loaded_bytes + expected <= BUSTER_PERF_AGGREGATE_LIMIT;
        dataset->loaded_bytes += expected;
        uint32_t path = perf_json_find(json, definition, "path");
        if (result && path != UINT32_MAX)
        {
            result = perf_read_below(dataset->root, perf_json_text(json, path), &resource->bytes);
        }
        else if (result)
        {
            result = perf_https_url(perf_json_text(json, perf_json_find(json, definition, "url")));
            bool loaded = result && dataset->cache != NULL && perf_read_below(dataset->cache, digest, &resource->bytes);
            if (result && !loaded && dataset->evidence_root != NULL)
            {
                resource->bytes.size = 0;
                uint32_t source = perf_json_find(json, definition, "source");
                loaded = perf_read_below(dataset->evidence_root, perf_json_text(json, perf_json_find(json, source, "path")), &resource->bytes);
            }
            result = result && loaded;
        }
        char actual[65];
        result = result && resource->bytes.size == expected && perf_sha256(perf_buffer_bytes(&resource->bytes), actual) && perf_bytes_equal(perf_literal(actual), digest);
        uint32_t format = perf_json_find(json, definition, "format");
        bool counted = false;
        if (result && perf_json_is(json, format, "arrow-ipc-stream"))
        {
            result = perf_arrow_inspect(perf_buffer_bytes(&resource->bytes), &resource->arrow);
            resource->rows = resource->arrow.rows;
            counted = true;
        }
        else if (result && perf_json_is(json, format, "jsonl"))
        {
            result = perf_jsonl_count(perf_buffer_bytes(&resource->bytes), &resource->rows);
            counted = true;
        }
        else if (result && perf_json_is(json, format, "perfetto-protobuf"))
        {
            uint64_t packets = 0;
            result = perf_trace_envelope(perf_buffer_bytes(&resource->bytes), &packets);
        }
        uint32_t declared = perf_json_find(json, definition, "rows");
        if (result && declared != UINT32_MAX)
        {
            uint64_t expected_rows = 0;
            result = counted && perf_uint64(perf_json_text(json, declared), &expected_rows) && expected_rows == resource->rows;
        }
        result = result || perf_error("missing, unsafe, oversized, corrupt or unsupported resource", perf_json_text(json, perf_json_find(json, definition, "id")));
    }
    return result;
}

static PerfResource *perf_resource_find(PerfDataset *dataset, PerfBytes id)
{
    PerfResource *result = NULL;
    for (size_t i = 0; i < dataset->resource_count && result == NULL; ++i)
    {
        uint32_t definition = dataset->resources[i].definition;
        if (perf_bytes_equal(perf_json_text(&dataset->document, perf_json_find(&dataset->document, definition, "id")), id))
        {
            result = &dataset->resources[i];
        }
    }
    return result;
}

static bool perf_table_parts(PerfDataset *dataset, PerfTable *table, uint32_t parts, PerfBuffer *output, uint64_t *rows)
{
    const PerfJson *json = &dataset->document;
    bool result = true;
    for (uint32_t part = perf_json_first(json, parts); part != UINT32_MAX && result; part = perf_json_after(json, parts, part))
    {
        for (uint32_t previous = perf_json_first(json, parts); previous != part && result; previous = perf_json_after(json, parts, previous))
        {
            result = !perf_bytes_equal(perf_json_text(json, previous), perf_json_text(json, part));
        }
        PerfResource *resource = perf_resource_find(dataset, perf_json_text(json, part));
        result = result && resource != NULL;
        if (!result)
        {
            break;
        }
        uint32_t format = perf_json_find(json, resource->definition, "format");
        if (perf_json_is(json, format, "arrow-ipc-stream"))
        {
            if (table->schema.fields == NULL)
            {
                table->schema = resource->arrow.schema;
            }
            result = perf_arrow_schemas_equal(&table->schema, &resource->arrow.schema);
            PerfArrowInfo read = {0};
            result = result && perf_arrow_read(perf_buffer_bytes(&resource->bytes), &read, output);
            table->json_mapping = table->json_mapping && read.json_mapping;
            *rows += read.rows;
            free(read.schema.fields);
        }
        else if (perf_json_is(json, format, "jsonl"))
        {
            result = table->schema.fields != NULL && perf_jsonl_read(perf_buffer_bytes(&resource->bytes), &table->schema, output, rows);
        }
        else
        {
            result = false;
        }
        result = result && *rows <= BUSTER_PERF_ROW_LIMIT;
    }
    return result;
}

static uint32_t perf_column(const PerfArrowSchema *schema, PerfBytes name)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t field = 0; field < schema->count && result == UINT32_MAX; field = schema->fields[field].next)
    {
        if (perf_bytes_equal(schema->fields[field].name, name))
        {
            result = field;
        }
    }
    return result;
}

static bool perf_column_reference(const PerfJson *json, const PerfTable *table, uint32_t name)
{
    bool result = name < json->node_count && json->nodes[name].kind == PERF_JSON_STRING && perf_column(&table->schema, perf_json_text(json, name)) != UINT32_MAX;
    return result;
}

static bool perf_table_row(const PerfTable *table, uint64_t row, PerfJson *document)
{
    bool result = row < table->count;
    if (result)
    {
        PerfBytes line = {
            .data = table->rows.data + table->offsets[row],
            .size = table->offsets[row + 1] - table->offsets[row] - 1
        };
        result = perf_json_parse(document, line);
    }
    return result;
}

static uint32_t perf_row_binding(const PerfJson *manifest, const PerfJson *row, uint32_t name)
{
    uint32_t result = name < manifest->node_count ? perf_json_find_bytes(row, 0, perf_json_text(manifest, name)) : UINT32_MAX;
    return result;
}

static bool perf_table_semantics(PerfDataset *dataset, const PerfTable *table)
{
    const PerfJson *json = &dataset->document;
    bool result = true;
    const char *lists[] = {"primary_key", "order_by", "dimensions"};
    for (size_t c = 0; c < sizeof(lists) / sizeof(lists[0]) && result; ++c)
    {
        uint32_t list = perf_json_find(json, table->definition, lists[c]);
        for (uint32_t name = perf_json_first(json, list); name != UINT32_MAX && result; name = perf_json_after(json, list, name))
        {
            result = perf_column_reference(json, table, name);
            for (uint32_t previous = perf_json_first(json, list); previous != name && result; previous = perf_json_after(json, list, previous))
            {
                result = !perf_bytes_equal(perf_json_text(json, previous), perf_json_text(json, name));
            }
        }
    }
    uint32_t measurements = perf_json_find(json, table->definition, "measurements");
    for (uint32_t column = perf_json_first(json, measurements); column != UINT32_MAX && result; column = perf_json_after(json, measurements, column))
    {
        uint32_t measurement = column + 1;
        result = perf_column_reference(json, table, column);
        const char *collections[] = {"metrics", "scopes", "instruments"};
        const char *references[] = {"metric_id", "scope_id", "instrument_id"};
        for (size_t c = 0; c < 3 && result; ++c)
        {
            result = perf_identity(json, collections[c], perf_json_text(json, perf_json_find(json, measurement, references[c]))) != UINT32_MAX;
        }
        const char *optional[] = {"status_column", "reason_column"};
        for (size_t c = 0; c < 2 && result; ++c)
        {
            uint32_t name = perf_json_find(json, measurement, optional[c]);
            result = name == UINT32_MAX || perf_column_reference(json, table, name);
        }
        uint32_t sample = perf_json_find(json, measurement, "sample_count");
        uint32_t sample_column = perf_json_find(json, sample, "column");
        result = result && (sample_column == UINT32_MAX || perf_column_reference(json, table, sample_column));
        uint32_t constant = perf_json_find(json, sample, "constant");
        if (result && constant != UINT32_MAX)
        {
            uint64_t ignored = 0;
            bool negative = false;
            result = perf_integer(perf_json_text(json, constant), 64, false, &negative, &ignored);
        }
        uint32_t intervals = perf_json_find(json, measurement, "intervals");
        for (uint32_t interval = perf_json_first(json, intervals); interval != UINT32_MAX && result; interval = perf_json_after(json, intervals, interval))
        {
            result = perf_column_reference(json, table, perf_json_find(json, interval, "low_column")) && perf_column_reference(json, table, perf_json_find(json, interval, "high_column"));
        }
        uint32_t quality = perf_json_find(json, measurement, "counter_quality_columns");
        for (uint32_t key = perf_json_first(json, quality); key != UINT32_MAX && result; key = perf_json_after(json, quality, key))
        {
            result = perf_column_reference(json, table, key + 1);
        }
    }
    uint32_t bindings = perf_json_find(json, table->definition, "bindings");
    uint32_t source = perf_json_find(json, bindings, "source_artifact");
    result = result && (source == UINT32_MAX || perf_resource_find(dataset, perf_json_text(json, source)) != NULL);
    uint32_t execution = perf_json_find(json, bindings, "execution_key");
    result = result && (execution == UINT32_MAX || perf_column_reference(json, table, execution));
    uint32_t time = perf_json_find(json, bindings, "time");
    result = result && (time == UINT32_MAX || perf_column_reference(json, table, perf_json_find(json, time, "column")));
    const char *context_bindings[] = {"protocol_context", "environment_context", "build_context"};
    const char *kinds[] = {"protocol", "environment", "build"};
    for (size_t c = 0; c < 3 && result; ++c)
    {
        uint32_t name = perf_json_find(json, bindings, context_bindings[c]);
        if (name != UINT32_MAX && perf_column(&table->schema, perf_json_text(json, name)) == UINT32_MAX)
        {
            uint32_t context = perf_identity(json, "contexts", perf_json_text(json, name));
            result = perf_json_is(json, perf_json_find(json, context, "kind"), kinds[c]);
        }
    }
    for (uint64_t r = 0; table->json_mapping && r < table->count && result; ++r)
    {
        PerfJson row = {0};
        result = perf_table_row(table, r, &row);
        for (size_t c = 0; c < 3 && result; ++c)
        {
            uint32_t name = perf_json_find(json, bindings, context_bindings[c]);
            if (name != UINT32_MAX)
            {
                PerfBytes id = perf_json_text(json, name);
                uint32_t value = perf_column(&table->schema, id) == UINT32_MAX ? UINT32_MAX : perf_row_binding(json, &row, name);
                id = value == UINT32_MAX ? id : perf_json_text(&row, value);
                uint32_t context = perf_identity(json, "contexts", id);
                result = perf_json_is(json, perf_json_find(json, context, "kind"), kinds[c]);
            }
        }
        for (uint32_t column = perf_json_first(json, measurements); column != UINT32_MAX && result; column = perf_json_after(json, measurements, column))
        {
            uint32_t measurement = column + 1;
            uint32_t value = perf_row_binding(json, &row, column);
            uint32_t status = perf_row_binding(json, &row, perf_json_find(json, measurement, "status_column"));
            uint32_t reason = perf_row_binding(json, &row, perf_json_find(json, measurement, "reason_column"));
            bool missing = value >= row.node_count || row.nodes[value].kind == PERF_JSON_NULL;
            bool ok = status == UINT32_MAX || perf_json_is(&row, status, "ok");
            if (missing || !ok)
            {
                result = status < row.node_count && row.nodes[status].kind == PERF_JSON_STRING && !ok && reason < row.node_count && row.nodes[reason].kind == PERF_JSON_STRING && perf_json_text(&row, reason).size != 0;
            }
            uint32_t sample = perf_json_find(json, measurement, "sample_count");
            uint32_t sample_column = perf_json_find(json, sample, "column");
            if (sample_column != UINT32_MAX && result)
            {
                uint32_t count = perf_row_binding(json, &row, sample_column);
                uint64_t ignored = 0;
                bool negative = false;
                result = count < row.node_count && (row.nodes[count].kind == PERF_JSON_NUMBER || row.nodes[count].kind == PERF_JSON_STRING) && perf_integer(perf_json_text(&row, count), 64, false, &negative, &ignored);
            }
        }
        perf_json_free(&row);
    }
    return result;
}

static bool perf_tables_load(PerfDataset *dataset)
{
    const PerfJson *json = &dataset->document;
    uint32_t definitions = perf_json_find(json, 0, "tables");
    dataset->table_count = perf_json_count(json, definitions);
    dataset->tables = calloc(dataset->table_count + 1, sizeof(*dataset->tables));
    bool result = dataset->tables != NULL;
    size_t index = 0;
    uint64_t retained_bytes = 0;
    for (uint32_t definition = perf_json_first(json, definitions); definition != UINT32_MAX && result; definition = perf_json_after(json, definitions, definition))
    {
        PerfTable *table = &dataset->tables[index++];
        table->definition = definition;
        table->json_mapping = true;
        uint32_t schema_id = perf_json_find(json, definition, "schema");
        if (schema_id != UINT32_MAX)
        {
            PerfResource *resource = perf_resource_find(dataset, perf_json_text(json, schema_id));
            result = resource != NULL && resource->arrow.schema.fields != NULL && resource->arrow.rows == 0;
            if (result)
            {
                table->schema = resource->arrow.schema;
            }
        }
        result = result && perf_table_parts(dataset, table, perf_json_find(json, definition, "data"), &table->rows, &table->count);
        dataset->rows += table->count;
        retained_bytes += table->rows.size;
        result = result && retained_bytes + dataset->loaded_bytes <= BUSTER_PERF_AGGREGATE_LIMIT;
        uint32_t alternatives = perf_json_find(json, definition, "alternatives");
        for (uint32_t parts = perf_json_first(json, alternatives); parts != UINT32_MAX && result; parts = perf_json_after(json, alternatives, parts))
        {
            PerfBuffer rows = {0};
            uint64_t count = 0;
            result = table->json_mapping && perf_table_parts(dataset, table, parts, &rows, &count) && table->json_mapping && count == table->count && perf_bytes_equal(perf_buffer_bytes(&rows), perf_buffer_bytes(&table->rows));
            dataset->cells_compared += result ? count * table->schema.roots : 0;
            free(rows.data);
        }
        if (result && !table->json_mapping)
        {
            ++dataset->native_only_tables;
            free(table->rows.data);
            table->rows = (PerfBuffer){0};
            result = perf_json_find(json, definition, "primary_key") == UINT32_MAX && perf_json_count(json, perf_json_find(json, definition, "foreign_keys")) == 0;
        }
        table->offsets = result ? calloc(table->json_mapping ? (size_t)table->count + 1 : 1, sizeof(*table->offsets)) : NULL;
        result = result && table->offsets != NULL;
        uint64_t r = 0;
        for (size_t c = 0; c < table->rows.size && result; ++c)
        {
            if (table->rows.data[c] == '\n')
            {
                result = r < table->count;
                if (result)
                {
                    table->offsets[++r] = (uint32_t)c + 1;
                }
            }
        }
        result = result && (!table->json_mapping || r == table->count) && perf_table_semantics(dataset, table);
        result = result || perf_error("table schema, exact alternative values or semantic bindings differ", perf_json_text(json, perf_json_find(json, definition, "id")));
    }
    return result;
}

typedef struct PerfKey PerfKey;
struct PerfKey
{
    char digest[65];
};

static int perf_key_compare(const void *left, const void *right)
{
    int result = strcmp(((const PerfKey *)left)->digest, ((const PerfKey *)right)->digest);
    return result;
}

static bool perf_table_keys(const PerfJson *manifest, const PerfTable *table, uint32_t columns, PerfKey **output, bool unique)
{
    bool result = table->json_mapping && perf_json_count(manifest, columns) != 0 && table->count <= BUSTER_PERF_ROW_LIMIT;
    PerfKey *keys = result ? calloc((size_t)table->count + 1, sizeof(*keys)) : NULL;
    result = result && keys != NULL;
    for (uint64_t r = 0; r < table->count && result; ++r)
    {
        PerfJson row = {0};
        PerfBuffer encoded = {0};
        result = perf_table_row(table, r, &row);
        for (uint32_t name = perf_json_first(manifest, columns); name != UINT32_MAX && result; name = perf_json_after(manifest, columns, name))
        {
            uint32_t field = perf_column(&table->schema, perf_json_text(manifest, name));
            uint32_t value = perf_row_binding(manifest, &row, name);
            result = field < table->schema.count && table->schema.fields[field].children == 0 && value < row.node_count && row.nodes[value].kind != PERF_JSON_NULL;
            if (result)
            {
                PerfJsonNode node = row.nodes[value];
                result = perf_buffer_byte(&encoded, (unsigned char)node.kind) && perf_json_quote(&encoded, (PerfBytes){.data = row.source.data + node.raw_offset, .size = node.raw_size});
            }
        }
        result = result && perf_sha256(perf_buffer_bytes(&encoded), keys[r].digest);
        perf_json_free(&row);
        free(encoded.data);
    }
    if (result)
    {
        qsort(keys, (size_t)table->count, sizeof(*keys), perf_key_compare);
        for (uint64_t r = 1; r < table->count && result && unique; ++r)
        {
            result = strcmp(keys[r - 1].digest, keys[r].digest) != 0;
        }
    }
    *output = keys;
    return result;
}

static bool perf_relationships(PerfDataset *dataset)
{
    const PerfJson *json = &dataset->document;
    bool result = true;
    for (size_t i = 0; i < dataset->table_count && result; ++i)
    {
        PerfTable *table = &dataset->tables[i];
        uint32_t primary = perf_json_find(json, table->definition, "primary_key");
        if (primary != UINT32_MAX)
        {
            PerfKey *keys = NULL;
            result = perf_table_keys(json, table, primary, &keys, true);
            free(keys);
        }
        uint32_t foreign_keys = perf_json_find(json, table->definition, "foreign_keys");
        for (uint32_t foreign = perf_json_first(json, foreign_keys); foreign != UINT32_MAX && result; foreign = perf_json_after(json, foreign_keys, foreign))
        {
            PerfBytes id = perf_json_text(json, perf_json_find(json, foreign, "target_table"));
            PerfTable *target = NULL;
            for (size_t t = 0; t < dataset->table_count && target == NULL; ++t)
            {
                if (perf_bytes_equal(id, perf_json_text(json, perf_json_find(json, dataset->tables[t].definition, "id"))))
                {
                    target = &dataset->tables[t];
                }
            }
            uint32_t source_columns = perf_json_find(json, foreign, "columns");
            uint32_t target_columns = perf_json_find(json, foreign, "target_columns");
            result = target != NULL && perf_json_count(json, source_columns) == perf_json_count(json, target_columns);
            uint32_t b = perf_json_first(json, target_columns);
            for (uint32_t a = perf_json_first(json, source_columns); a != UINT32_MAX && result; a = perf_json_after(json, source_columns, a))
            {
                uint32_t s = perf_column(&table->schema, perf_json_text(json, a));
                uint32_t t = perf_column(&target->schema, perf_json_text(json, b));
                result = s < table->schema.count && t < target->schema.count;
                if (result)
                {
                    const PerfArrowField *left = &table->schema.fields[s];
                    const PerfArrowField *right = &target->schema.fields[t];
                    result = left->children == 0 && right->children == 0 && left->type == right->type && left->bit_width == right->bit_width && left->precision == right->precision && left->scale == right->scale && left->is_signed == right->is_signed && left->fixed_size == right->fixed_size && strcmp(left->metadata, right->metadata) == 0;
                }
                b = perf_json_after(json, target_columns, b);
            }
            PerfKey *source_keys = NULL;
            PerfKey *target_keys = NULL;
            result = result && perf_table_keys(json, table, source_columns, &source_keys, false) && perf_table_keys(json, target, target_columns, &target_keys, true);
            for (uint64_t r = 0; r < table->count && result; ++r)
            {
                result = bsearch(&source_keys[r], target_keys, (size_t)target->count, sizeof(*target_keys), perf_key_compare) != NULL;
            }
            free(source_keys);
            free(target_keys);
        }
    }
    result = result || perf_error("null/nonunique key or unresolved/typed foreign key", (PerfBytes){0});
    return result;
}

static void perf_dataset_free(PerfDataset *dataset)
{
    for (size_t i = 0; dataset->resources != NULL && i < dataset->resource_count; ++i)
    {
        free(dataset->resources[i].bytes.data);
        free(dataset->resources[i].arrow.schema.fields);
    }
    for (size_t i = 0; dataset->tables != NULL && i < dataset->table_count; ++i)
    {
        free(dataset->tables[i].rows.data);
        free(dataset->tables[i].offsets);
    }
    free(dataset->resources);
    free(dataset->tables);
    perf_json_free(&dataset->document);
}

static bool perf_dataset_load(PerfDataset *dataset, const char *path, const char *schema_path)
{
    char resolved[4096];
    bool result = realpath(path, resolved) != NULL;
    if (result)
    {
        memcpy(dataset->root, resolved, strlen(resolved) + 1);
        char *slash = strrchr(dataset->root, '/');
        result = slash != NULL;
        if (result)
        {
            slash[1] = 0;
        }
    }
    PerfBuffer bytes = {0};
    PerfBuffer schema_bytes = {0};
    PerfJson schema = {0};
    result = result && perf_read_file(resolved, &bytes) && perf_json_parse(&dataset->document, perf_buffer_bytes(&bytes)) && perf_read_file(schema_path, &schema_bytes) && perf_json_parse(&schema, perf_buffer_bytes(&schema_bytes)) && perf_schema_validate(&schema, &dataset->document) && perf_metadata(&dataset->document);
    result = result && perf_resources_load(dataset) && perf_tables_load(dataset) && perf_relationships(dataset);
    if (!result)
    {
        perf_error("dataset rejected; generation is not validated", perf_literal(path));
    }
    free(bytes.data);
    free(schema_bytes.data);
    perf_json_free(&schema);
    return result;
}
