/* Strict JSON and the canonical schema's JSON Schema 2020-12 vocabulary.
 * Explicit task trees bound work and avoid input-dependent C recursion.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
typedef struct BmfJsonWalk BmfJsonWalk;
struct BmfJsonWalk
{
    yyjson_val *value;
    unsigned int depth;
};

static bool bmf_json_unique(yyjson_val *root)
{
    BmfJsonWalk *walk = calloc(BUSTER_BMF_TASKS, sizeof(*walk));
    bool result = walk != NULL;
    size_t count = result ? 1 : 0;
    if (result)
    {
        walk[0].value = root;
    }
    for (size_t i = 0; i < count && result; ++i)
    {
        yyjson_val *value = walk[i].value;
        result = walk[i].depth < BUSTER_BMF_DEPTH;
        if (yyjson_is_obj(value))
        {
            size_t index, length;
            yyjson_val *key, *child;
            yyjson_obj_foreach(value, index, length, key, child)
            {
                size_t key_length = yyjson_get_len(key);
                result = result && strlen(bmf_string(key)) == key_length;
                result = result && yyjson_obj_getn(value, bmf_string(key), key_length) == child;
                result = result && count < BUSTER_BMF_TASKS;
                if (result)
                {
                    walk[count++] = (BmfJsonWalk){.value = child, .depth = walk[i].depth + 1};
                }
            }
        }
        else if (yyjson_is_arr(value))
        {
            size_t index, length;
            yyjson_val *child;
            yyjson_arr_foreach(value, index, length, child)
            {
                result = result && count < BUSTER_BMF_TASKS;
                if (result)
                {
                    walk[count++] = (BmfJsonWalk){.value = child, .depth = walk[i].depth + 1};
                }
            }
        }
        else if (yyjson_is_str(value))
        {
            result = result && strlen(bmf_string(value)) == yyjson_get_len(value);
        }
    }
    free(walk);
    return result;
}

static yyjson_doc *bmf_json_read(const unsigned char *bytes, size_t size)
{
    yyjson_read_err error = {0};
    yyjson_doc *result = yyjson_read_opts((char *)bytes, size, YYJSON_READ_NOFLAG, NULL, &error);
    if (result != NULL && !bmf_json_unique(yyjson_doc_get_root(result)))
    {
        yyjson_doc_free(result);
        result = NULL;
        bmf_error("duplicate keys, embedded NUL or JSON work/depth limit", "");
    }
    if (result == NULL)
    {
        bmf_error("invalid JSON", error.msg);
    }
    return result;
}

static yyjson_doc *bmf_json_file(const char *path)
{
    BmfBuffer bytes = {0};
    yyjson_doc *result = NULL;
    if (bmf_file_read(path, &bytes))
    {
        result = bmf_json_read(bytes.data, bytes.size);
    }
    free(bytes.data);
    return result;
}

static bool bmf_regex(const char *pattern, const char *text)
{
    int error;
    PCRE2_SIZE offset;
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, PCRE2_UTF | PCRE2_UCP, &error, &offset, NULL);
    pcre2_match_data *match = code == NULL ? NULL : pcre2_match_data_create_from_pattern(code, NULL);
    bool result = match != NULL && pcre2_match(code, (PCRE2_SPTR)text, strlen(text), 0, 0, match, NULL) >= 0;
    pcre2_match_data_free(match);
    pcre2_code_free(code);
    return result;
}

static bool bmf_timestamp(const char *text)
{
    bool result = bmf_regex("^[0-9]{4}-[0-9]{2}-[0-9]{2}[Tt][0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-9]+)?([Zz]|[+-][0-9]{2}:[0-9]{2})\\z", text);
    if (result)
    {
        int year, month, day, hour, minute, second;
        result = sscanf(text, "%4d-%2d-%2d%*c%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) == 6;
        static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        int maximum = month >= 1 && month <= 12 ? days[month] + (month == 2 && leap) : 0;
        result = result && day >= 1 && day <= maximum && hour <= 23 && minute <= 59 && second <= 59;
        size_t size = strlen(text);
        if (size >= 6 && (text[size - 6] == '+' || text[size - 6] == '-'))
        {
            int zone_hour, zone_minute;
            result = result && sscanf(text + size - 5, "%2d:%2d", &zone_hour, &zone_minute) == 2;
            result = result && zone_hour <= 23 && zone_minute <= 59;
        }
    }
    return result;
}

typedef struct BmfSchemaTask BmfSchemaTask;
struct BmfSchemaTask
{
    yyjson_val *schema;
    yyjson_val *value;
    size_t parent;
    unsigned int operation;
    unsigned int children;
    unsigned int passing;
    bool valid;
};

static bool bmf_schema_push(BmfSchemaTask *tasks, size_t *count, size_t parent, yyjson_val *schema, yyjson_val *value, unsigned int operation)
{
    bool result = *count < BUSTER_BMF_TASKS && schema != NULL;
    if (result)
    {
        tasks[*count] = (BmfSchemaTask){.schema = schema, .value = value, .parent = parent, .operation = operation, .valid = true};
        if (*count != 0)
        {
            ++tasks[parent].children;
        }
        ++*count;
    }
    return result;
}

static bool bmf_type_matches(yyjson_val *value, const char *name)
{
    bool result = (strcmp(name, "object") == 0 && yyjson_is_obj(value)) || (strcmp(name, "array") == 0 && yyjson_is_arr(value)) || (strcmp(name, "string") == 0 && yyjson_is_str(value)) || (strcmp(name, "boolean") == 0 && yyjson_is_bool(value)) || (strcmp(name, "integer") == 0 && (yyjson_is_int(value) || (yyjson_is_real(value) && isfinite(yyjson_get_real(value)) && trunc(yyjson_get_real(value)) == yyjson_get_real(value)))) || (strcmp(name, "number") == 0 && yyjson_is_num(value)) || (strcmp(name, "null") == 0 && yyjson_is_null(value));
    return result;
}

static bool bmf_schema_check(yyjson_val *root, yyjson_val *value)
{
    BmfSchemaTask *tasks = calloc(BUSTER_BMF_TASKS, sizeof(*tasks));
    size_t count = 0;
    bool result = tasks != NULL && bmf_schema_push(tasks, &count, 0, root, value, 0);
    for (size_t i = 0; i < count && result; ++i)
    {
        BmfSchemaTask *task = &tasks[i];
        yyjson_val *schema = task->schema;
        yyjson_val *data = task->value;
        if (task->operation != 0)
        {
            continue;
        }
        if (yyjson_is_bool(schema))
        {
            task->valid = yyjson_get_bool(schema);
            continue;
        }
        const char *reference = yyjson_get_str(bmf_get(schema, "$ref"));
        if (reference != NULL)
        {
            result = strncmp(reference, "#/$defs/", 8) == 0 && bmf_schema_push(tasks, &count, i, bmf_get(bmf_get(root, "$defs"), reference + 8), data, 0);
        }
        yyjson_val *constraint = bmf_get(schema, "type");
        if (constraint != NULL)
        {
            task->valid = task->valid && bmf_type_matches(data, bmf_string(constraint));
        }
        constraint = bmf_get(schema, "const");
        task->valid = task->valid && (constraint == NULL || yyjson_equals(data, constraint));
        constraint = bmf_get(schema, "enum");
        if (constraint != NULL)
        {
            size_t index, length;
            yyjson_val *option;
            bool found = false;
            yyjson_arr_foreach(constraint, index, length, option)
            {
                found |= yyjson_equals(data, option);
            }
            task->valid = task->valid && found;
        }
        if (yyjson_is_num(data))
        {
            yyjson_val *lower = bmf_get(schema, "minimum");
            yyjson_val *upper = bmf_get(schema, "maximum");
            task->valid = task->valid && (lower == NULL || yyjson_get_num(data) >= yyjson_get_num(lower)) && (upper == NULL || yyjson_get_num(data) <= yyjson_get_num(upper));
            lower = bmf_get(schema, "exclusiveMinimum");
            upper = bmf_get(schema, "exclusiveMaximum");
            task->valid = task->valid && (lower == NULL || yyjson_get_num(data) > yyjson_get_num(lower)) && (upper == NULL || yyjson_get_num(data) < yyjson_get_num(upper));
        }
        if (yyjson_is_str(data))
        {
            constraint = bmf_get(schema, "pattern");
            task->valid = task->valid && (constraint == NULL || bmf_regex(bmf_string(constraint), bmf_string(data)));
            constraint = bmf_get(schema, "minLength");
            size_t codepoints = 0;
            const unsigned char *text = (const unsigned char *)bmf_string(data);
            for (size_t c = 0; c < yyjson_get_len(data); ++c)
            {
                codepoints += (text[c] & 0xc0) != 0x80;
            }
            task->valid = task->valid && (constraint == NULL || codepoints >= yyjson_get_uint(constraint));
            constraint = bmf_get(schema, "maxLength");
            task->valid = task->valid && (constraint == NULL || codepoints <= yyjson_get_uint(constraint));
            constraint = bmf_get(schema, "format");
            task->valid = task->valid && (!yyjson_equals_str(constraint, "date-time") || bmf_timestamp(bmf_string(data)));
        }
        if (yyjson_is_arr(data))
        {
            constraint = bmf_get(schema, "minItems");
            task->valid = task->valid && (constraint == NULL || yyjson_arr_size(data) >= yyjson_get_uint(constraint));
            constraint = bmf_get(schema, "maxItems");
            task->valid = task->valid && (constraint == NULL || yyjson_arr_size(data) <= yyjson_get_uint(constraint));
            constraint = bmf_get(schema, "items");
            size_t index, length;
            yyjson_val *child;
            yyjson_arr_foreach(data, index, length, child)
            {
                if (constraint != NULL && result)
                {
                    result = bmf_schema_push(tasks, &count, i, constraint, child, 0);
                }
            }
        }
        if (yyjson_is_obj(data))
        {
            size_t index, length;
            yyjson_val *key, *child;
            yyjson_arr_foreach(bmf_get(schema, "required"), index, length, key)
            {
                task->valid = task->valid && bmf_get(data, bmf_string(key)) != NULL;
            }
            yyjson_val *properties = bmf_get(schema, "properties");
            yyjson_val *additional = bmf_get(schema, "additionalProperties");
            yyjson_obj_foreach(data, index, length, key, child)
            {
                constraint = bmf_get(properties, bmf_string(key));
                constraint = constraint == NULL ? additional : constraint;
                if (constraint != NULL && result)
                {
                    result = bmf_schema_push(tasks, &count, i, constraint, child, 0);
                }
            }
        }
        constraint = bmf_get(schema, "oneOf");
        if (constraint != NULL && result)
        {
            size_t group = count;
            result = bmf_schema_push(tasks, &count, i, constraint, data, 1);
            size_t index, length;
            yyjson_val *child;
            yyjson_arr_foreach(constraint, index, length, child)
            {
                result = result && bmf_schema_push(tasks, &count, group, child, data, 0);
            }
        }
        constraint = bmf_get(schema, "not");
        if (constraint != NULL && result)
        {
            size_t group = count;
            result = bmf_schema_push(tasks, &count, i, constraint, data, 2) && bmf_schema_push(tasks, &count, group, constraint, data, 0);
        }
    }
    for (size_t remaining = count; remaining > 0 && result; --remaining)
    {
        size_t i = remaining - 1;
        BmfSchemaTask *task = &tasks[i];
        task->valid = task->valid && (task->operation == 1 ? task->passing == 1 : task->operation == 2 ? task->passing == 0 : task->passing == task->children);
        if (i != 0)
        {
            tasks[task->parent].passing += task->valid;
        }
        else
        {
            result = task->valid;
        }
    }
    free(tasks);
    if (!result)
    {
        bmf_error("manifest does not satisfy canonical schema", "");
    }
    return result;
}

static bool bmf_uint_string(yyjson_val *value, uint64_t *output)
{
    const char *text = bmf_string(value);
    bool result = bmf_regex("^(0|[1-9][0-9]*)\\z", text);
    uint64_t number = 0;
    for (size_t i = 0; text[i] != 0 && result; ++i)
    {
        unsigned int digit = (unsigned int)(text[i] - '0');
        result = number <= (UINT64_MAX - digit) / 10;
        if (result)
        {
            number = number * 10 + digit;
        }
    }
    *output = number;
    return result;
}

static yyjson_val *bmf_find(yyjson_val *array, const char *id)
{
    yyjson_val *result = NULL;
    size_t index, length;
    yyjson_val *value;
    yyjson_arr_foreach(array, index, length, value)
    {
        if (yyjson_equals_str(bmf_get(value, "id"), id))
        {
            result = value;
        }
    }
    return result;
}

static bool bmf_metadata_check(yyjson_val *root)
{
    bool result = true;
    const char *collections[] = {"resources", "tables", "metrics", "scopes", "instruments", "contexts", "traces"};
    for (size_t c = 0; c < sizeof(collections) / sizeof(collections[0]) && result; ++c)
    {
        yyjson_val *array = bmf_get(root, collections[c]);
        size_t index, length;
        yyjson_val *value;
        yyjson_arr_foreach(array, index, length, value)
        {
            const char *id = bmf_string(bmf_get(value, "id"));
            result = result && bmf_regex("^[A-Za-z0-9][A-Za-z0-9._:-]{0,159}\\z", id) && bmf_find(array, id) == value;
        }
    }
    size_t index, length;
    yyjson_val *value;
    yyjson_arr_foreach(bmf_get(root, "requires"), index, length, value)
    {
        result = result && (yyjson_equals_str(value, "arrow-ipc") || yyjson_equals_str(value, "jsonl.arrow-mapping.v1") || yyjson_equals_str(value, "perfetto.protobuf"));
    }
    yyjson_arr_foreach(bmf_get(root, "contexts"), index, length, value)
    {
        size_t e, n;
        yyjson_val *reference;
        yyjson_arr_foreach(bmf_get(value, "evidence"), e, n, reference)
        {
            result = result && bmf_find(bmf_get(root, "resources"), bmf_string(reference)) != NULL;
        }
    }
    yyjson_arr_foreach(bmf_get(root, "traces"), index, length, value)
    {
        yyjson_val *resource = bmf_find(bmf_get(root, "resources"), bmf_string(bmf_get(value, "resource")));
        yyjson_val *execution = bmf_find(bmf_get(root, "contexts"), bmf_string(bmf_get(value, "execution_id")));
        result = result && yyjson_equals_str(bmf_get(resource, "format"), "perfetto-protobuf") && yyjson_equals_str(bmf_get(execution, "kind"), "execution");
        size_t e, n;
        yyjson_val *reference;
        yyjson_arr_foreach(bmf_get(value, "source_artifacts"), e, n, reference)
        {
            result = result && bmf_find(bmf_get(root, "resources"), bmf_string(reference)) != NULL;
        }
    }
    if (!result)
    {
        bmf_error("duplicate/unresolved identity or unsupported required capability", "");
    }
    return result;
}
