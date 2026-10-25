/* Native nanoarrow IPC reading and exact JSONL equivalence. Arrays and schemas
 * are validated before unsafe element getters. Unknown physical types remain
 * native-only and cannot silently acquire a JSON mapping.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
typedef struct BmfSchemaPair BmfSchemaPair;
struct BmfSchemaPair
{
    const struct ArrowSchema *left;
    const struct ArrowSchema *right;
};

static bool bmf_schemas_equal(const struct ArrowSchema *left, const struct ArrowSchema *right)
{
    BmfSchemaPair *walk = calloc(BUSTER_BMF_TASKS, sizeof(*walk));
    bool result = walk != NULL;
    size_t count = result ? 1 : 0;
    if (result)
    {
        walk[0] = (BmfSchemaPair){.left = left, .right = right};
    }
    for (size_t i = 0; i < count && result; ++i)
    {
        left = walk[i].left;
        right = walk[i].right;
        result = strcmp(left->format, right->format) == 0 && strcmp(left->name == NULL ? "" : left->name, right->name == NULL ? "" : right->name) == 0 && left->flags == right->flags && left->n_children == right->n_children;
        int64_t left_size = ArrowMetadataSizeOf(left->metadata);
        int64_t right_size = ArrowMetadataSizeOf(right->metadata);
        result = result && left_size == right_size && (left_size == 0 || memcmp(left->metadata, right->metadata, (size_t)left_size) == 0);
        result = result && (left->dictionary == NULL) == (right->dictionary == NULL);
        if (result && left->dictionary != NULL)
        {
            result = count < BUSTER_BMF_TASKS;
            if (result)
            {
                walk[count++] = (BmfSchemaPair){.left = left->dictionary, .right = right->dictionary};
            }
        }
        for (int64_t j = 0; j < left->n_children && result; ++j)
        {
            result = count < BUSTER_BMF_TASKS;
            if (result)
            {
                walk[count++] = (BmfSchemaPair){.left = left->children[j], .right = right->children[j]};
            }
        }
    }
    free(walk);
    return result;
}

static bool bmf_schema_fields(const struct ArrowSchema *schema)
{
    const struct ArrowSchema **walk = calloc(BUSTER_BMF_TASKS, sizeof(*walk));
    bool result = walk != NULL;
    size_t count = result ? 1 : 0;
    if (result)
    {
        walk[0] = schema;
    }
    for (size_t i = 0; i < count && result; ++i)
    {
        schema = walk[i];
        struct ArrowSchemaView view;
        result = ArrowSchemaViewInit(&view, schema, NULL) == NANOARROW_OK;
        for (int64_t j = 0; j < schema->n_children && result; ++j)
        {
            const char *name = schema->children[j]->name;
            if (view.type == NANOARROW_TYPE_STRUCT)
            {
                result = name != NULL;
                for (int64_t k = 0; k < j && result; ++k)
                {
                    result = strcmp(name, schema->children[k]->name) != 0;
                }
            }
            result = result && count < BUSTER_BMF_TASKS;
            if (result)
            {
                walk[count++] = schema->children[j];
            }
        }
    }
    free(walk);
    return result;
}

typedef struct BmfArrowCell BmfArrowCell;
struct BmfArrowCell
{
    const struct ArrowSchema *schema;
    const struct ArrowArrayView *view;
    int64_t row;
    yyjson_mut_val *parent;
    const char *key;
    unsigned int depth;
};

static yyjson_mut_val *bmf_arrow_row(yyjson_mut_doc *document, const struct ArrowSchema *schema, const struct ArrowArrayView *view, int64_t row, bool *mapped)
{
    BmfArrowCell *walk = calloc(BUSTER_BMF_TASKS, sizeof(*walk));
    bool valid = walk != NULL;
    size_t count = valid ? 1 : 0;
    yyjson_mut_val *result = NULL;
    if (valid)
    {
        walk[0] = (BmfArrowCell){.schema = schema, .view = view, .row = row};
    }
    for (size_t i = 0; i < count && valid; ++i)
    {
        BmfArrowCell cell = walk[i];
        struct ArrowSchemaView type;
        valid = cell.depth < BUSTER_BMF_DEPTH && ArrowSchemaViewInit(&type, cell.schema, NULL) == NANOARROW_OK;
        yyjson_mut_val *value = NULL;
        int64_t start = 0;
        int64_t end = 0;
        const struct ArrowSchema *child_schema = NULL;
        const struct ArrowArrayView *child_view = NULL;
        if (valid && ArrowArrayViewIsNull(cell.view, cell.row))
        {
            valid = (cell.schema->flags & ARROW_FLAG_NULLABLE) != 0;
            value = yyjson_mut_null(document);
        }
        else if (valid)
        {
            char text[128];
            switch (type.type)
            {
                case NANOARROW_TYPE_BOOL:
                    value = yyjson_mut_bool(document, ArrowArrayViewGetIntUnsafe(cell.view, cell.row) != 0);
                    break;
                case NANOARROW_TYPE_INT8:
                case NANOARROW_TYPE_INT16:
                case NANOARROW_TYPE_INT32:
                    value = yyjson_mut_sint(document, ArrowArrayViewGetIntUnsafe(cell.view, cell.row));
                    break;
                case NANOARROW_TYPE_UINT8:
                case NANOARROW_TYPE_UINT16:
                case NANOARROW_TYPE_UINT32:
                    value = yyjson_mut_uint(document, ArrowArrayViewGetUIntUnsafe(cell.view, cell.row));
                    break;
                case NANOARROW_TYPE_INT64:
                    snprintf(text, sizeof(text), "%" PRId64, ArrowArrayViewGetIntUnsafe(cell.view, cell.row));
                    value = yyjson_mut_strcpy(document, text);
                    break;
                case NANOARROW_TYPE_UINT64:
                    snprintf(text, sizeof(text), "%" PRIu64, ArrowArrayViewGetUIntUnsafe(cell.view, cell.row));
                    value = yyjson_mut_strcpy(document, text);
                    break;
                case NANOARROW_TYPE_HALF_FLOAT:
                case NANOARROW_TYPE_FLOAT:
                case NANOARROW_TYPE_DOUBLE:
                {
                    double number = ArrowArrayViewGetDoubleUnsafe(cell.view, cell.row);
                    if (isfinite(number))
                    {
                        value = yyjson_mut_real(document, number);
                    }
                    else
                    {
                        *mapped = false;
                        value = yyjson_mut_null(document);
                    }
                } break;
                case NANOARROW_TYPE_STRING:
                case NANOARROW_TYPE_LARGE_STRING:
                {
                    struct ArrowStringView string = ArrowArrayViewGetStringUnsafe(cell.view, cell.row);
                    value = yyjson_mut_strncpy(document, string.data, (size_t)string.size_bytes);
                } break;
                case NANOARROW_TYPE_BINARY:
                case NANOARROW_TYPE_LARGE_BINARY:
                case NANOARROW_TYPE_FIXED_SIZE_BINARY:
                {
                    struct ArrowBufferView bytes = ArrowArrayViewGetBytesUnsafe(cell.view, cell.row);
                    valid = bytes.size_bytes <= BUSTER_BMF_BYTES / 2;
                    size_t length = valid ? 4 * (((size_t)bytes.size_bytes + 2) / 3) : 0;
                    unsigned char *encoded = valid ? malloc(length + 1) : NULL;
                    valid = valid && encoded != NULL;
                    if (valid)
                    {
                        EVP_EncodeBlock(encoded, bytes.data.as_uint8, (int)bytes.size_bytes);
                        value = yyjson_mut_strncpy(document, (char *)encoded, length);
                    }
                    free(encoded);
                } break;
                case NANOARROW_TYPE_DECIMAL32:
                case NANOARROW_TYPE_DECIMAL64:
                case NANOARROW_TYPE_DECIMAL128:
                case NANOARROW_TYPE_DECIMAL256:
                {
                    struct ArrowDecimal decimal;
                    struct ArrowBuffer buffer;
                    ArrowBufferInit(&buffer);
                    ArrowDecimalInit(&decimal, type.decimal_bitwidth, type.decimal_precision, type.decimal_scale);
                    ArrowArrayViewGetDecimalUnsafe(cell.view, cell.row, &decimal);
                    bool supported_scale = type.decimal_scale >= -76 && type.decimal_scale <= 76;
                    valid = !supported_scale || ArrowDecimalAppendStringToBuffer(&decimal, &buffer) == NANOARROW_OK;
                    if (valid && supported_scale)
                    {
                        value = yyjson_mut_strncpy(document, (const char *)buffer.data, (size_t)buffer.size_bytes);
                    }
                    else if (valid)
                    {
                        *mapped = false;
                        value = yyjson_mut_null(document);
                    }
                    ArrowBufferReset(&buffer);
                } break;
                case NANOARROW_TYPE_STRUCT:
                    value = yyjson_mut_obj(document);
                    break;
                case NANOARROW_TYPE_LIST:
                case NANOARROW_TYPE_LARGE_LIST:
                    value = yyjson_mut_arr(document);
                    start = ArrowArrayViewListChildOffset(cell.view, cell.row);
                    end = ArrowArrayViewListChildOffset(cell.view, cell.row + 1);
                    child_schema = cell.schema->children[0];
                    child_view = cell.view->children[0];
                    break;
                case NANOARROW_TYPE_FIXED_SIZE_LIST:
                    value = yyjson_mut_arr(document);
                    start = (cell.row + cell.view->offset) * type.fixed_size;
                    end = start + type.fixed_size;
                    child_schema = cell.schema->children[0];
                    child_view = cell.view->children[0];
                    break;
                case NANOARROW_TYPE_NA:
                    value = yyjson_mut_null(document);
                    break;
                default:
                    *mapped = false;
                    value = yyjson_mut_null(document);
                    break;
            }
            if (valid && type.type == NANOARROW_TYPE_STRUCT)
            {
                for (int64_t c = 0; c < cell.schema->n_children && valid; ++c)
                {
                    valid = count < BUSTER_BMF_TASKS;
                    if (valid)
                    {
                        walk[count++] = (BmfArrowCell){.schema = cell.schema->children[c], .view = cell.view->children[c], .row = cell.row + cell.view->offset, .parent = value, .key = cell.schema->children[c]->name, .depth = cell.depth + 1};
                    }
                }
            }
            for (int64_t c = start; c < end && valid; ++c)
            {
                valid = count < BUSTER_BMF_TASKS;
                if (valid)
                {
                    walk[count++] = (BmfArrowCell){.schema = child_schema, .view = child_view, .row = c, .parent = value, .depth = cell.depth + 1};
                }
            }
        }
        valid = valid && value != NULL;
        if (valid && cell.parent != NULL)
        {
            yyjson_mut_val *key = cell.key == NULL ? NULL : yyjson_mut_strcpy(document, cell.key);
            valid = cell.key == NULL ? yyjson_mut_arr_append(cell.parent, value) : key != NULL && yyjson_mut_obj_add(cell.parent, key, value);
        }
        else if (valid)
        {
            result = value;
        }
    }
    free(walk);
    if (!valid)
    {
        result = NULL;
    }
    return result;
}

static bool bmf_arrow_read(BmfResource *resource, struct ArrowSchema *schema, yyjson_mut_doc *rows_document, yyjson_mut_val *rows, uint64_t *row_count, bool *mapped)
{
    struct ArrowBuffer buffer;
    ArrowBufferInit(&buffer);
    struct ArrowIpcInputStream input = {0};
    struct ArrowArrayStream stream = {0};
    struct ArrowArrayView view = {0};
    struct ArrowError error = {0};
    struct ArrowArray array = {0};
    bool result = ArrowBufferAppend(&buffer, resource->bytes.data, (int64_t)resource->bytes.size) == NANOARROW_OK;
    result = result && ArrowIpcInputStreamInitBuffer(&input, &buffer) == NANOARROW_OK;
    result = result && ArrowIpcArrayStreamReaderInit(&stream, &input, NULL) == NANOARROW_OK;
    result = result && ArrowArrayStreamGetSchema(&stream, schema, &error) == NANOARROW_OK;
    result = result && strcmp(schema->format, "+s") == 0 && bmf_schema_fields(schema);
    result = result && ArrowArrayViewInitFromSchema(&view, schema, &error) == NANOARROW_OK;
    uint64_t count = 0;
    bool done = false;
    while (result && !done)
    {
        result = ArrowArrayStreamGetNext(&stream, &array, &error) == NANOARROW_OK;
        done = array.release == NULL;
        if (result && !done)
        {
            result = ArrowArrayViewSetArray(&view, &array, &error) == NANOARROW_OK && ArrowArrayViewValidate(&view, NANOARROW_VALIDATION_LEVEL_FULL, &error) == NANOARROW_OK;
            result = result && (uint64_t)array.length <= UINT64_MAX - count;
            for (int64_t r = 0; r < array.length && result; ++r)
            {
                if (rows_document != NULL)
                {
                    yyjson_mut_val *row = bmf_arrow_row(rows_document, schema, &view, r, mapped);
                    result = row != NULL && yyjson_mut_arr_append(rows, row);
                }
            }
            count += (uint64_t)array.length;
        }
        if (array.release != NULL)
        {
            ArrowArrayRelease(&array);
        }
    }
    yyjson_val *declared_rows = bmf_get(resource->definition, "rows");
    if (result && declared_rows != NULL)
    {
        uint64_t expected;
        result = bmf_uint_string(declared_rows, &expected) && count == expected;
    }
    *row_count = count;
    ArrowArrayViewReset(&view);
    if (stream.release != NULL)
    {
        ArrowArrayStreamRelease(&stream);
    }
    if (input.release != NULL)
    {
        input.release(&input);
    }
    ArrowBufferReset(&buffer);
    if (!result)
    {
        bmf_error("invalid Arrow IPC/schema/row count", error.message);
    }
    return result;
}

static yyjson_mut_val *bmf_decimal_make(yyjson_mut_doc *document, const char *text, const struct ArrowSchemaView *type)
{
    bool valid = strlen(text) < 160 && type->decimal_scale >= -76 && type->decimal_scale <= 76 && bmf_regex("^-?(0|[1-9][0-9]*)(\\.[0-9]+)?\\z", text);
    const char *point = strchr(text, '.');
    size_t fraction = point == NULL ? 0 : strlen(point + 1);
    char digits[256];
    size_t count = 0;
    for (size_t i = 0; text[i] != 0 && valid; ++i)
    {
        if (text[i] >= '0' && text[i] <= '9')
        {
            digits[count++] = text[i];
        }
    }
    int64_t shift = (int64_t)type->decimal_scale - (int64_t)fraction;
    for (int64_t i = 0; i < shift && valid; ++i)
    {
        valid = count < sizeof(digits) - 2;
        if (valid)
        {
            digits[count++] = '0';
        }
    }
    for (int64_t i = 0; i > shift && valid; --i)
    {
        valid = count == 0 || digits[count - 1] == '0';
        if (valid && count > 0)
        {
            --count;
        }
    }
    size_t first = 0;
    while (first < count && digits[first] == '0')
    {
        ++first;
    }
    valid = valid && count - first <= (size_t)type->decimal_precision;
    char unscaled[256];
    size_t length = 0;
    if (valid && text[0] == '-' && first != count)
    {
        unscaled[length++] = '-';
    }
    if (valid)
    {
        if (first == count)
        {
            unscaled[length++] = '0';
        }
        else
        {
            memcpy(unscaled + length, digits + first, count - first);
            length += count - first;
        }
    }
    unscaled[length] = 0;
    struct ArrowDecimal decimal;
    struct ArrowBuffer buffer;
    ArrowBufferInit(&buffer);
    ArrowDecimalInit(&decimal, type->decimal_bitwidth, type->decimal_precision, type->decimal_scale);
    valid = valid && ArrowDecimalSetDigits(&decimal, ArrowCharView(unscaled)) == NANOARROW_OK && ArrowDecimalAppendStringToBuffer(&decimal, &buffer) == NANOARROW_OK;
    yyjson_mut_val *result = valid ? yyjson_mut_strncpy(document, (const char *)buffer.data, (size_t)buffer.size_bytes) : NULL;
    ArrowBufferReset(&buffer);
    return result;
}

typedef struct BmfTypedJson BmfTypedJson;
struct BmfTypedJson
{
    const struct ArrowSchema *schema;
    yyjson_val *value;
    unsigned int depth;
};

static bool bmf_json_typed(const struct ArrowSchema *schema, yyjson_val *row)
{
    BmfTypedJson *walk = calloc(BUSTER_BMF_TASKS, sizeof(*walk));
    bool result = walk != NULL;
    size_t count = result ? 1 : 0;
    if (result)
    {
        walk[0] = (BmfTypedJson){.schema = schema, .value = row};
    }
    for (size_t i = 0; i < count && result; ++i)
    {
        schema = walk[i].schema;
        yyjson_val *value = walk[i].value;
        struct ArrowSchemaView type;
        result = walk[i].depth < BUSTER_BMF_DEPTH && ArrowSchemaViewInit(&type, schema, NULL) == NANOARROW_OK;
        if (yyjson_is_null(value))
        {
            result = result && (schema->flags & ARROW_FLAG_NULLABLE) != 0;
            continue;
        }
        if (result)
        {
            switch (type.type)
            {
                case NANOARROW_TYPE_BOOL:
                    result = yyjson_is_bool(value);
                    break;
                case NANOARROW_TYPE_INT8:
                case NANOARROW_TYPE_INT16:
                case NANOARROW_TYPE_INT32:
                {
                    int bits = type.type == NANOARROW_TYPE_INT8 ? 8 : type.type == NANOARROW_TYPE_INT16 ? 16 : 32;
                    int64_t number = yyjson_get_sint(value);
                    result = yyjson_is_int(value) && (!yyjson_is_uint(value) || yyjson_get_uint(value) <= INT32_MAX) && number >= -(INT64_C(1) << (bits - 1)) && number < (INT64_C(1) << (bits - 1));
                } break;
                case NANOARROW_TYPE_UINT8:
                case NANOARROW_TYPE_UINT16:
                case NANOARROW_TYPE_UINT32:
                {
                    int bits = type.type == NANOARROW_TYPE_UINT8 ? 8 : type.type == NANOARROW_TYPE_UINT16 ? 16 : 32;
                    result = yyjson_is_int(value) && (!yyjson_is_sint(value) || yyjson_get_sint(value) >= 0) && yyjson_get_uint(value) < (UINT64_C(1) << bits);
                } break;
                case NANOARROW_TYPE_UINT64:
                {
                    uint64_t number;
                    result = yyjson_is_str(value) && bmf_uint_string(value, &number);
                } break;
                case NANOARROW_TYPE_INT64:
                {
                    const char *text = bmf_string(value);
                    result = yyjson_is_str(value) && bmf_regex("^-?(0|[1-9][0-9]*)\\z", text);
                    errno = 0;
                    char *end;
                    strtoll(text, &end, 10);
                    result = result && errno == 0 && end[0] == 0;
                } break;
                case NANOARROW_TYPE_HALF_FLOAT:
                case NANOARROW_TYPE_FLOAT:
                case NANOARROW_TYPE_DOUBLE:
                {
                    double number = yyjson_get_num(value);
                    result = yyjson_is_num(value) && isfinite(number);
                    if (type.type == NANOARROW_TYPE_FLOAT)
                    {
                        result = result && isfinite((float)number);
                    }
                    if (type.type == NANOARROW_TYPE_HALF_FLOAT)
                    {
                        result = result && isfinite(ArrowHalfFloatToFloat(ArrowFloatToHalfFloat((float)number)));
                    }
                } break;
                case NANOARROW_TYPE_STRING:
                case NANOARROW_TYPE_LARGE_STRING:
                    result = yyjson_is_str(value);
                    break;
                case NANOARROW_TYPE_BINARY:
                case NANOARROW_TYPE_LARGE_BINARY:
                case NANOARROW_TYPE_FIXED_SIZE_BINARY:
                {
                    const char *text = bmf_string(value);
                    size_t length = strlen(text);
                    result = yyjson_is_str(value) && bmf_regex("^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?\\z", text);
                    size_t decoded = length / 4 * 3 - (length > 0 && text[length - 1] == '=') - (length > 1 && text[length - 2] == '=');
                    result = result && (type.type != NANOARROW_TYPE_FIXED_SIZE_BINARY || decoded == (size_t)type.fixed_size);
                } break;
                case NANOARROW_TYPE_DECIMAL32:
                case NANOARROW_TYPE_DECIMAL64:
                case NANOARROW_TYPE_DECIMAL128:
                case NANOARROW_TYPE_DECIMAL256:
                {
                    yyjson_mut_doc *temporary = yyjson_mut_doc_new(NULL);
                    result = yyjson_is_str(value) && temporary != NULL && bmf_decimal_make(temporary, bmf_string(value), &type) != NULL;
                    yyjson_mut_doc_free(temporary);
                }
                    break;
                case NANOARROW_TYPE_STRUCT:
                    result = yyjson_is_obj(value) && yyjson_obj_size(value) == (size_t)schema->n_children;
                    for (int64_t c = 0; c < schema->n_children && result; ++c)
                    {
                        yyjson_val *child = bmf_get(value, schema->children[c]->name);
                        result = child != NULL && count < BUSTER_BMF_TASKS;
                        if (result)
                        {
                            walk[count++] = (BmfTypedJson){.schema = schema->children[c], .value = child, .depth = walk[i].depth + 1};
                        }
                    }
                    break;
                case NANOARROW_TYPE_LIST:
                case NANOARROW_TYPE_LARGE_LIST:
                case NANOARROW_TYPE_FIXED_SIZE_LIST:
                {
                    result = yyjson_is_arr(value) && (type.type != NANOARROW_TYPE_FIXED_SIZE_LIST || yyjson_arr_size(value) == (size_t)type.fixed_size);
                    size_t index, length;
                    yyjson_val *child;
                    yyjson_arr_foreach(value, index, length, child)
                    {
                        result = result && count < BUSTER_BMF_TASKS;
                        if (result)
                        {
                            walk[count++] = (BmfTypedJson){.schema = schema->children[0], .value = child, .depth = walk[i].depth + 1};
                        }
                    }
                } break;
                default:
                    result = false;
                    break;
            }
        }
        if (!result)
        {
            fprintf(stderr, "bmf: JSON mapping rejected field '%s' (Arrow %s)\n", schema->name == NULL ? "" : schema->name, schema->format);
        }
    }
    free(walk);
    return result;
}

typedef struct BmfMutableJson BmfMutableJson;
struct BmfMutableJson
{
    const struct ArrowSchema *schema;
    yyjson_mut_val *value;
};

static bool bmf_json_normalize(yyjson_mut_doc *document, yyjson_mut_val *row, const struct ArrowSchema *schema)
{
    BmfMutableJson *walk = calloc(BUSTER_BMF_TASKS, sizeof(*walk));
    bool result = walk != NULL;
    size_t count = result ? 1 : 0;
    if (result)
    {
        walk[0] = (BmfMutableJson){.schema = schema, .value = row};
    }
    for (size_t i = 0; i < count && result; ++i)
    {
        schema = walk[i].schema;
        yyjson_mut_val *value = walk[i].value;
        struct ArrowSchemaView type;
        result = ArrowSchemaViewInit(&type, schema, NULL) == NANOARROW_OK;
        if (yyjson_mut_is_null(value))
        {
            continue;
        }
        if (result && (type.type == NANOARROW_TYPE_DECIMAL32 || type.type == NANOARROW_TYPE_DECIMAL64 || type.type == NANOARROW_TYPE_DECIMAL128 || type.type == NANOARROW_TYPE_DECIMAL256))
        {
            yyjson_mut_val *canonical = bmf_decimal_make(document, yyjson_mut_get_str(value), &type);
            result = canonical != NULL && yyjson_mut_set_strn(value, yyjson_mut_get_str(canonical), yyjson_mut_get_len(canonical));
        }
        else if (result && (type.type == NANOARROW_TYPE_FLOAT || type.type == NANOARROW_TYPE_HALF_FLOAT))
        {
            double number = yyjson_mut_get_num(value);
            number = type.type == NANOARROW_TYPE_FLOAT ? (double)(float)number : ArrowHalfFloatToFloat(ArrowFloatToHalfFloat((float)number));
            result = yyjson_mut_set_real(value, number);
        }
        else if (result && type.type == NANOARROW_TYPE_STRUCT)
        {
            for (int64_t c = 0; c < schema->n_children && result; ++c)
            {
                result = count < BUSTER_BMF_TASKS;
                if (result)
                {
                    walk[count++] = (BmfMutableJson){.schema = schema->children[c], .value = yyjson_mut_obj_get(value, schema->children[c]->name)};
                }
            }
        }
        else if (result && yyjson_mut_is_arr(value))
        {
            size_t index, length;
            yyjson_mut_val *child;
            yyjson_mut_arr_foreach(value, index, length, child)
            {
                result = result && count < BUSTER_BMF_TASKS;
                if (result)
                {
                    walk[count++] = (BmfMutableJson){.schema = schema->children[0], .value = child};
                }
            }
        }
    }
    free(walk);
    return result;
}

static bool bmf_jsonl_read(BmfResource *resource, const struct ArrowSchema *schema, yyjson_mut_doc *document, yyjson_mut_val *rows, uint64_t *row_count)
{
    bool result = true;
    size_t start = 0;
    uint64_t count = 0;
    for (size_t end = 0; end <= resource->bytes.size && result; ++end)
    {
        if (end == resource->bytes.size || resource->bytes.data[end] == '\n')
        {
            size_t size = end - start;
            bool blank = true;
            for (size_t c = start; c < end; ++c)
            {
                blank = blank && isspace(resource->bytes.data[c]);
            }
            if (!blank)
            {
                yyjson_doc *row = bmf_json_read(resource->bytes.data + start, size);
                result = row != NULL && bmf_json_typed(schema, yyjson_doc_get_root(row));
                if (result)
                {
                    yyjson_mut_val *copy = yyjson_val_mut_copy(document, yyjson_doc_get_root(row));
                    result = copy != NULL && bmf_json_normalize(document, copy, schema) && yyjson_mut_arr_append(rows, copy);
                    ++count;
                }
                yyjson_doc_free(row);
            }
            start = end + 1;
        }
    }
    yyjson_val *declared = bmf_get(resource->definition, "rows");
    if (result && declared != NULL)
    {
        uint64_t expected;
        result = bmf_uint_string(declared, &expected) && expected == count;
    }
    *row_count = count;
    if (!result)
    {
        bmf_error("JSONL mapping/schema/row count mismatch", bmf_string(bmf_get(resource->definition, "id")));
    }
    return result;
}

static bool bmf_table_parts(BmfDataset *dataset, BmfTable *table, yyjson_val *parts, yyjson_doc **output, uint64_t *total, bool *mapped)
{
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *rows = document == NULL ? NULL : yyjson_mut_arr(document);
    bool result = rows != NULL;
    uint64_t count = 0;
    size_t index, length;
    yyjson_val *id;
    yyjson_arr_foreach(parts, index, length, id)
    {
        BmfResource *resource = bmf_resource_find(dataset, bmf_string(id));
        result = result && resource != NULL;
        for (size_t p = 0; p < index && result; ++p)
        {
            result = !yyjson_equals(id, yyjson_arr_get(parts, p));
        }
        uint64_t part_count = 0;
        if (result && yyjson_equals_str(bmf_get(resource->definition, "format"), "arrow-ipc-stream"))
        {
            struct ArrowSchema schema = {0};
            result = bmf_arrow_read(resource, &schema, document, rows, &part_count, mapped);
            if (result && table->schema.release == NULL)
            {
                ArrowSchemaMove(&schema, &table->schema);
            }
            else if (result)
            {
                result = bmf_schemas_equal(&table->schema, &schema);
            }
            if (schema.release != NULL)
            {
                ArrowSchemaRelease(&schema);
            }
        }
        else if (result && yyjson_equals_str(bmf_get(resource->definition, "format"), "jsonl"))
        {
            result = bmf_get(table->definition, "schema") != NULL && table->schema.release != NULL && bmf_jsonl_read(resource, &table->schema, document, rows, &part_count);
        }
        else
        {
            result = false;
        }
        result = result && part_count <= UINT64_MAX - count;
        count += part_count;
    }
    if (result)
    {
        yyjson_mut_doc_set_root(document, rows);
        *output = yyjson_mut_doc_imut_copy(document, NULL);
        result = *output != NULL;
    }
    *total = count;
    yyjson_mut_doc_free(document);
    return result;
}

static int64_t bmf_column_index(BmfTable *table, const char *name)
{
    int64_t result = -1;
    for (int64_t i = 0; i < table->schema.n_children; ++i)
    {
        if (strcmp(table->schema.children[i]->name, name) == 0)
        {
            result = i;
        }
    }
    return result;
}

static bool bmf_column_ref(BmfTable *table, yyjson_val *name)
{
    bool result = yyjson_is_str(name) && bmf_column_index(table, bmf_string(name)) >= 0;
    if (!result)
    {
        bmf_error("unresolved exact Arrow field name", bmf_string(name));
    }
    return result;
}

static bool bmf_table_semantics(BmfDataset *dataset, BmfTable *table)
{
    yyjson_val *root = yyjson_doc_get_root(dataset->document);
    bool result = true;
    const char *column_lists[] = {"primary_key", "order_by", "dimensions"};
    for (size_t c = 0; c < sizeof(column_lists) / sizeof(column_lists[0]); ++c)
    {
        size_t index, length;
        yyjson_val *name;
        yyjson_arr_foreach(bmf_get(table->definition, column_lists[c]), index, length, name)
        {
            result = result && bmf_column_ref(table, name);
        }
    }
    size_t index, length;
    yyjson_val *column, *measurement;
    yyjson_obj_foreach(bmf_get(table->definition, "measurements"), index, length, column, measurement)
    {
        result = result && bmf_column_ref(table, column);
        result = result && bmf_find(bmf_get(root, "metrics"), bmf_string(bmf_get(measurement, "metric_id"))) != NULL;
        result = result && bmf_find(bmf_get(root, "scopes"), bmf_string(bmf_get(measurement, "scope_id"))) != NULL;
        result = result && bmf_find(bmf_get(root, "instruments"), bmf_string(bmf_get(measurement, "instrument_id"))) != NULL;
        const char *optional[] = {"status_column", "reason_column"};
        for (size_t c = 0; c < sizeof(optional) / sizeof(optional[0]); ++c)
        {
            yyjson_val *name = bmf_get(measurement, optional[c]);
            result = result && (name == NULL || bmf_column_ref(table, name));
        }
        yyjson_val *sample = bmf_get(measurement, "sample_count");
        yyjson_val *sample_column = bmf_get(sample, "column");
        if (sample_column != NULL)
        {
            result = result && bmf_column_ref(table, sample_column);
        }
        yyjson_val *constant = bmf_get(sample, "constant");
        if (yyjson_is_str(constant))
        {
            uint64_t ignored;
            result = result && bmf_uint_string(constant, &ignored);
        }
        else if (constant != NULL)
        {
            result = result && yyjson_is_int(constant) && (!yyjson_is_sint(constant) || yyjson_get_sint(constant) >= 0) && yyjson_get_uint(constant) <= UINT64_C(9007199254740991);
        }
        yyjson_val *intervals = bmf_get(measurement, "intervals");
        size_t interval_index, interval_count;
        yyjson_val *interval;
        yyjson_arr_foreach(intervals, interval_index, interval_count, interval)
        {
            result = result && bmf_column_ref(table, bmf_get(interval, "low_column")) && bmf_column_ref(table, bmf_get(interval, "high_column"));
        }
        yyjson_val *quality_columns = bmf_get(measurement, "counter_quality_columns");
        size_t quality_index, quality_count;
        yyjson_val *quality_key, *quality_column;
        yyjson_obj_foreach(quality_columns, quality_index, quality_count, quality_key, quality_column)
        {
            result = result && bmf_column_ref(table, quality_column);
        }
        size_t r, n;
        yyjson_val *row;
        yyjson_arr_foreach(yyjson_doc_get_root(table->rows), r, n, row)
        {
            yyjson_val *value = bmf_get(row, bmf_string(column));
            yyjson_val *status = bmf_get(row, bmf_string(bmf_get(measurement, "status_column")));
            yyjson_val *reason = bmf_get(row, bmf_string(bmf_get(measurement, "reason_column")));
            bool missing = yyjson_is_null(value);
            bool ok = status == NULL || yyjson_equals_str(status, "ok");
            if (table->json_mapping && (missing || !ok))
            {
                result = result && status != NULL && !ok && yyjson_is_str(reason) && yyjson_get_len(reason) > 0;
            }
            if (table->json_mapping && sample_column != NULL)
            {
                yyjson_val *number = bmf_get(row, bmf_string(sample_column));
                uint64_t ignored;
                result = result && (yyjson_is_str(number) ? bmf_uint_string(number, &ignored) : yyjson_is_int(number) && (!yyjson_is_sint(number) || yyjson_get_sint(number) >= 0));
            }
        }
    }
    yyjson_val *bindings = bmf_get(table->definition, "bindings");
    const char *contexts[] = {"protocol_context", "environment_context", "build_context"};
    const char *kinds[] = {"protocol", "environment", "build"};
    for (size_t c = 0; c < sizeof(contexts) / sizeof(contexts[0]) && result; ++c)
    {
        yyjson_val *id = bmf_get(bindings, contexts[c]);
        if (id != NULL && bmf_column_index(table, bmf_string(id)) >= 0)
        {
            size_t r, n;
            yyjson_val *row;
            yyjson_arr_foreach(yyjson_doc_get_root(table->rows), r, n, row)
            {
                yyjson_val *context = bmf_find(bmf_get(root, "contexts"), bmf_string(bmf_get(row, bmf_string(id))));
                result = result && yyjson_equals_str(bmf_get(context, "kind"), kinds[c]);
            }
        }
        else if (id != NULL)
        {
            yyjson_val *context = bmf_find(bmf_get(root, "contexts"), bmf_string(id));
            result = result && yyjson_equals_str(bmf_get(context, "kind"), kinds[c]);
        }
    }
    yyjson_val *source = bmf_get(bindings, "source_artifact");
    result = result && (source == NULL || bmf_find(bmf_get(root, "resources"), bmf_string(source)) != NULL);
    yyjson_val *execution_key = bmf_get(bindings, "execution_key");
    result = result && (execution_key == NULL || bmf_column_ref(table, execution_key));
    yyjson_val *time = bmf_get(bindings, "time");
    result = result && (time == NULL || bmf_column_ref(table, bmf_get(time, "column")));
    return result;
}

static bool bmf_tables_load(BmfDataset *dataset)
{
    yyjson_val *definitions = bmf_get(yyjson_doc_get_root(dataset->document), "tables");
    dataset->table_count = yyjson_arr_size(definitions);
    dataset->tables = calloc(dataset->table_count + 1, sizeof(*dataset->tables));
    bool result = dataset->tables != NULL;
    for (size_t i = 0; i < dataset->table_count && result; ++i)
    {
        BmfTable *table = &dataset->tables[i];
        table->definition = yyjson_arr_get(definitions, i);
        table->json_mapping = true;
        yyjson_val *schema_id = bmf_get(table->definition, "schema");
        if (schema_id != NULL)
        {
            BmfResource *schema = bmf_resource_find(dataset, bmf_string(schema_id));
            uint64_t count = 0;
            result = schema != NULL && yyjson_equals_str(bmf_get(schema->definition, "format"), "arrow-ipc-stream") && bmf_arrow_read(schema, &table->schema, NULL, NULL, &count, &table->json_mapping) && count == 0;
        }
        result = result && bmf_table_parts(dataset, table, bmf_get(table->definition, "data"), &table->rows, &table->row_count, &table->json_mapping);
        dataset->total_rows += table->row_count;
        size_t alternative, alternative_count;
        yyjson_val *parts;
        yyjson_arr_foreach(bmf_get(table->definition, "alternatives"), alternative, alternative_count, parts)
        {
            yyjson_doc *rows = NULL;
            uint64_t count = 0;
            bool mapped = true;
            result = result && table->json_mapping && bmf_table_parts(dataset, table, parts, &rows, &count, &mapped) && mapped && count == table->row_count;
            bool equivalent = rows != NULL && yyjson_equals(yyjson_doc_get_root(rows), yyjson_doc_get_root(table->rows));
            if (result && !equivalent)
            {
                bmf_error("alternative exact typed values differ", bmf_string(bmf_get(table->definition, "id")));
            }
            result = result && equivalent;
            if (result)
            {
                dataset->compared_cells += count * (uint64_t)table->schema.n_children;
            }
            yyjson_doc_free(rows);
        }
        result = result && bmf_table_semantics(dataset, table);
        if (!result)
        {
            bmf_error("table schema, binding or alternative mismatch", bmf_string(bmf_get(table->definition, "id")));
        }
    }
    return result;
}

typedef struct BmfKey BmfKey;
struct BmfKey
{
    char digest[65];
};

static int bmf_key_compare(const void *left, const void *right)
{
    int result = strcmp(((const BmfKey *)left)->digest, ((const BmfKey *)right)->digest);
    return result;
}

static bool bmf_keys(BmfTable *table, yyjson_val *columns, BmfKey **output, bool unique)
{
    size_t size = yyjson_arr_size(columns);
    bool result = size != 0 && table->json_mapping && table->row_count < SIZE_MAX / sizeof(BmfKey);
    BmfKey *keys = result ? calloc((size_t)table->row_count + 1, sizeof(*keys)) : NULL;
    result = result && keys != NULL;
    for (size_t r = 0; r < table->row_count && result; ++r)
    {
        BmfBuffer encoded = {0};
        yyjson_val *row = yyjson_arr_get(yyjson_doc_get_root(table->rows), r);
        for (size_t c = 0; c < size && result; ++c)
        {
            yyjson_val *name = yyjson_arr_get(columns, c);
            yyjson_val *cell = bmf_get(row, bmf_string(name));
            result = bmf_column_ref(table, name) && cell != NULL && !yyjson_is_null(cell);
            size_t length = 0;
            char *text = result ? yyjson_val_write(cell, 0, &length) : NULL;
            char prefix[32];
            int prefix_length = snprintf(prefix, sizeof(prefix), "%zu:", length);
            result = result && text != NULL && bmf_buffer_append(&encoded, prefix, (size_t)prefix_length) && bmf_buffer_append(&encoded, text, length);
            free(text);
        }
        result = result && bmf_sha256(encoded.data, encoded.size, keys[r].digest);
        free(encoded.data);
    }
    if (result)
    {
        qsort(keys, (size_t)table->row_count, sizeof(*keys), bmf_key_compare);
        for (size_t r = 1; r < table->row_count && result && unique; ++r)
        {
            result = strcmp(keys[r - 1].digest, keys[r].digest) != 0;
        }
    }
    *output = keys;
    return result;
}

static bool bmf_relationships_check(BmfDataset *dataset)
{
    bool result = true;
    for (size_t i = 0; i < dataset->table_count && result; ++i)
    {
        BmfTable *table = &dataset->tables[i];
        yyjson_val *primary = bmf_get(table->definition, "primary_key");
        if (primary != NULL)
        {
            BmfKey *keys = NULL;
            result = bmf_keys(table, primary, &keys, true);
            free(keys);
        }
        size_t index, length;
        yyjson_val *foreign;
        yyjson_arr_foreach(bmf_get(table->definition, "foreign_keys"), index, length, foreign)
        {
            BmfTable *target = NULL;
            for (size_t t = 0; t < dataset->table_count; ++t)
            {
                if (yyjson_equals_str(bmf_get(dataset->tables[t].definition, "id"), bmf_string(bmf_get(foreign, "target_table"))))
                {
                    target = &dataset->tables[t];
                }
            }
            yyjson_val *source_columns = bmf_get(foreign, "columns");
            yyjson_val *target_columns = bmf_get(foreign, "target_columns");
            result = result && target != NULL && yyjson_arr_size(source_columns) == yyjson_arr_size(target_columns);
            for (size_t c = 0; c < yyjson_arr_size(source_columns) && result; ++c)
            {
                int64_t source_index = bmf_column_index(table, bmf_string(yyjson_arr_get(source_columns, c)));
                int64_t target_index = bmf_column_index(target, bmf_string(yyjson_arr_get(target_columns, c)));
                result = source_index >= 0 && target_index >= 0;
                if (result)
                {
                    const struct ArrowSchema *source_type = table->schema.children[source_index];
                    const struct ArrowSchema *target_type = target->schema.children[target_index];
                    result = strcmp(source_type->format, target_type->format) == 0 && source_type->n_children == 0 && target_type->n_children == 0 && source_type->dictionary == NULL && target_type->dictionary == NULL;
                }
            }
            BmfKey *source_keys = NULL;
            BmfKey *target_keys = NULL;
            result = result && bmf_keys(table, source_columns, &source_keys, false) && bmf_keys(target, target_columns, &target_keys, true);
            for (size_t r = 0; r < table->row_count && result; ++r)
            {
                result = bsearch(&source_keys[r], target_keys, (size_t)target->row_count, sizeof(*target_keys), bmf_key_compare) != NULL;
            }
            free(source_keys);
            free(target_keys);
        }
    }
    if (!result)
    {
        bmf_error("nonunique/null key or unresolved foreign-key relationship", "");
    }
    return result;
}
