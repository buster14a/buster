/* Real-source workload descriptor preflight for the native throughput tool.
 * Ownership: this file parses a bounded, line-oriented descriptor and verifies
 * its staged input inventory. It does not run an oracle or admit a workload.
 * Map: tp_workload_descriptor_parse (syntax and schema),
 * tp_workload_descriptor_inputs (content/inventory closure),
 * tp_workload_check (read-only JSON preflight report).
 */
#ifndef BUSTER_THROUGHPUT_WORKLOAD_H
#define BUSTER_THROUGHPUT_WORKLOAD_H

#define TP_WORKLOAD_DESCRIPTOR_SCHEMA "buster-throughput-workload-v1"
#define TP_WORKLOAD_MAX_INPUTS 96
#define TP_WORKLOAD_MAX_ARGUMENTS 32
#define TP_WORKLOAD_TEXT_CAP 512

typedef struct TpWorkloadInput
{
    char role[16];
    char path[TP_WORKLOAD_TEXT_CAP];
    char sha256[65];
    uint64_t bytes;
} TpWorkloadInput;

typedef struct TpWorkloadDescriptor
{
    char schema[64];
    char name[128];
    char family[64];
    char source_identity[256];
    char dependency_identity[256];
    char generated_identity[256];
    char resource_identity[256];
    char sysroot_identity[256];
    char sdk_identity[256];
    char environment_identity[256];
    char target[128];
    char abi[128];
    char cpu_features[128];
    char c_lowerings[128];
    char pic_modes[64];
    char allocator_modes[128];
    char operations[128];
    char artifacts[128];
    char oracle[256];
    char historical_outcome[32];
    char historical_evidence[256];
    char admission[64];
    char cwd[TP_WORKLOAD_TEXT_CAP];
    char input_tree_sha256[65];
    uint64_t requested_translation_unit_bytes;
    TpWorkloadInput inputs[TP_WORKLOAD_MAX_INPUTS];
    unsigned input_count;
    char compile_arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP];
    unsigned compile_argument_count;
    char link_arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP];
    unsigned link_argument_count;
} TpWorkloadDescriptor;

typedef struct TpWorkloadCheckOptions
{
    char const* descriptor;
    char const* source_root;
    char const* compiler;
    char const* evidence;
    char const* evidence_outcome;
} TpWorkloadCheckOptions;

typedef struct TpDescriptorScalar
{
    char const* key;
    char* value;
    size_t capacity;
    uint64_t bit;
} TpDescriptorScalar;

static int tp_workload_text_copy(char* output, size_t capacity, char const* value)
{
    size_t length = strlen(value);
    int ok = length > 0 && length < capacity;
    for (size_t i = 0; i < length && ok; ++i)
    {
        unsigned char c = (unsigned char)value[i];
        ok = c >= 32 && c < 127;
    }
    if (ok) memcpy(output, value, length + 1);
    return ok;
}

static int tp_workload_sha256(char const* value)
{
    int ok = strlen(value) == 64;
    for (unsigned i = 0; i < 64 && ok; ++i)
    {
        char c = value[i];
        ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return ok;
}

static int tp_workload_relative_path(char const* path)
{
    int ok = path[0] && path[0] != '/' && path[0] != '\\' && !strchr(path, '\\') && !strchr(path, ':');
    char const* part = path;
    for (char const* p = path; ok; ++p)
    {
        if (*p == '/' || !*p)
        {
            size_t length = (size_t)(p - part);
            ok = length && !(length == 1 && part[0] == '.') && !(length == 2 && part[0] == '.' && part[1] == '.');
            part = p + 1;
            if (!*p) break;
        }
    }
    return ok;
}

static int tp_workload_u64(char const* text, uint64_t* value)
{
    String8 string = string_from_pointer(text);
    IntegerParsingU64 parsed = string8_parse_u64_decimal(string);
    int ok = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == string.length;
    if (ok) *value = parsed.value;
    return ok;
}

static int tp_workload_input_parse(char* value, TpWorkloadInput* input)
{
    char* fields[4] = {value};
    unsigned count = 1;
    int ok = 1;
    for (char* p = value; *p && ok; ++p)
    {
        if (*p == '\t')
        {
            *p = 0;
            ok = count < 4;
            if (ok) fields[count++] = p + 1;
        }
    }
    ok = ok && count == 4 &&
         (!strcmp(fields[0], "source") || !strcmp(fields[0], "header") ||
          !strcmp(fields[0], "generated") || !strcmp(fields[0], "dependency")) &&
         tp_workload_text_copy(input->role, sizeof(input->role), fields[0]) &&
         tp_workload_relative_path(fields[1]) &&
         tp_workload_text_copy(input->path, sizeof(input->path), fields[1]) &&
         tp_workload_sha256(fields[2]) && tp_workload_u64(fields[3], &input->bytes) && input->bytes > 0;
    if (ok) memcpy(input->sha256, fields[2], sizeof(input->sha256));
    return ok;
}

static int tp_workload_arguments_valid(char arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP],
                                       unsigned count, int link)
{
    unsigned compiler = 0, output = 0, output_option = 0, operation = 0, source = 0;
    unsigned mode = 0, frontend = 0, pic = 0, metrics = 0, objects = 0, root = 0;
    int ok = count >= 2 && !strcmp(arguments[0], "$COMPILER") && !strcmp(arguments[1], "cc");
    for (unsigned i = 0; i < count && ok; ++i)
    {
        char const* value = arguments[i];
        int known_placeholder = !strcmp(value, "$COMPILER") || !strcmp(value, "$OUTPUT") ||
                                (!link && (!strcmp(value, "$SOURCE") || !strcmp(value, "$FRONTEND") ||
                                           !strcmp(value, "$PIC") || !strcmp(value, "-I$ROOT") ||
                                           !strcmp(value, "-fregister-allocator=$MODE") ||
                                           !strcmp(value, "-fsource-metrics=$METRICS"))) ||
                                (link && !strcmp(value, "$OBJECTS"));
        ok = value[0] && !strchr(value, '\t') && (known_placeholder || !strchr(value, '$'));
        compiler += !strcmp(value, "$COMPILER");
        output += !strcmp(value, "$OUTPUT");
        output_option += !strcmp(value, "-o");
        operation += !strcmp(value, link ? "cc" : "-c");
        source += !strcmp(value, "$SOURCE");
        mode += !strcmp(value, "-fregister-allocator=$MODE");
        frontend += !strcmp(value, "$FRONTEND");
        pic += !strcmp(value, "$PIC");
        metrics += !strcmp(value, "-fsource-metrics=$METRICS");
        objects += !strcmp(value, "$OBJECTS");
        root += !strcmp(value, "-I$ROOT");
    }
    unsigned output_index = count;
    for (unsigned i = 0; i < count; ++i) if (!strcmp(arguments[i], "-o")) output_index = i;
    ok = ok && output_option == 1 && output_index + 1 < count && !strcmp(arguments[output_index + 1], "$OUTPUT");
    if (link) ok = ok && compiler == 1 && output == 1 && operation == 1 && objects == 1 && !source && !mode && !frontend && !pic && !metrics && !root;
    else ok = ok && compiler == 1 && output == 1 && operation == 1 && source == 1 && mode == 1 && frontend == 1 && pic == 1 && metrics == 1 && root == 1 && !objects;
    return ok;
}

static int tp_workload_descriptor_parse(char const* path, TpWorkloadDescriptor* descriptor)
{
    memset(descriptor, 0, sizeof(*descriptor));
    char requested_bytes[32] = {0};
    TpDescriptorScalar scalars[] = {
        {"schema", descriptor->schema, sizeof(descriptor->schema), UINT64_C(1) << 0},
        {"name", descriptor->name, sizeof(descriptor->name), UINT64_C(1) << 1},
        {"family", descriptor->family, sizeof(descriptor->family), UINT64_C(1) << 2},
        {"source_identity", descriptor->source_identity, sizeof(descriptor->source_identity), UINT64_C(1) << 3},
        {"dependency_identity", descriptor->dependency_identity, sizeof(descriptor->dependency_identity), UINT64_C(1) << 4},
        {"generated_identity", descriptor->generated_identity, sizeof(descriptor->generated_identity), UINT64_C(1) << 5},
        {"resource_identity", descriptor->resource_identity, sizeof(descriptor->resource_identity), UINT64_C(1) << 6},
        {"sysroot_identity", descriptor->sysroot_identity, sizeof(descriptor->sysroot_identity), UINT64_C(1) << 7},
        {"sdk_identity", descriptor->sdk_identity, sizeof(descriptor->sdk_identity), UINT64_C(1) << 8},
        {"environment_identity", descriptor->environment_identity, sizeof(descriptor->environment_identity), UINT64_C(1) << 9},
        {"target", descriptor->target, sizeof(descriptor->target), UINT64_C(1) << 10},
        {"abi", descriptor->abi, sizeof(descriptor->abi), UINT64_C(1) << 11},
        {"cpu_features", descriptor->cpu_features, sizeof(descriptor->cpu_features), UINT64_C(1) << 12},
        {"c_lowerings", descriptor->c_lowerings, sizeof(descriptor->c_lowerings), UINT64_C(1) << 13},
        {"pic_modes", descriptor->pic_modes, sizeof(descriptor->pic_modes), UINT64_C(1) << 14},
        {"allocator_modes", descriptor->allocator_modes, sizeof(descriptor->allocator_modes), UINT64_C(1) << 15},
        {"operations", descriptor->operations, sizeof(descriptor->operations), UINT64_C(1) << 16},
        {"artifacts", descriptor->artifacts, sizeof(descriptor->artifacts), UINT64_C(1) << 17},
        {"oracle", descriptor->oracle, sizeof(descriptor->oracle), UINT64_C(1) << 18},
        {"historical_outcome", descriptor->historical_outcome, sizeof(descriptor->historical_outcome), UINT64_C(1) << 19},
        {"historical_evidence", descriptor->historical_evidence, sizeof(descriptor->historical_evidence), UINT64_C(1) << 20},
        {"admission", descriptor->admission, sizeof(descriptor->admission), UINT64_C(1) << 21},
        {"cwd", descriptor->cwd, sizeof(descriptor->cwd), UINT64_C(1) << 22},
        {"requested_translation_unit_bytes", requested_bytes, sizeof(requested_bytes), UINT64_C(1) << 23},
        {"input_tree_sha256", descriptor->input_tree_sha256, sizeof(descriptor->input_tree_sha256), UINT64_C(1) << 24},
    };
    uint64_t seen = 0;
    uint64_t required = (UINT64_C(1) << BUSTER_ARRAY_LENGTH(scalars)) - 1;
    FILE* file = fopen(path, "rb");
    int ok = file != NULL;
    char line[4096];
    unsigned line_number = 0;
    while (ok && fgets(line, sizeof(line), file))
    {
        ++line_number;
        size_t length = strlen(line);
        ok = length && line[length - 1] == '\n';
        if (ok)
        {
            line[--length] = 0;
            if (length && line[length - 1] == '\r') line[--length] = 0;
            char* equal = strchr(line, '=');
            ok = equal && equal != line && equal[1];
            if (ok)
            {
                *equal = 0;
                char* value = equal + 1;
                int handled = 0;
                for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(scalars) && !handled; ++i)
                {
                    if (!strcmp(line, scalars[i].key))
                    {
                        handled = 1;
                        ok = !(seen & scalars[i].bit) && tp_workload_text_copy(scalars[i].value, scalars[i].capacity, value);
                        if (ok) seen |= scalars[i].bit;
                    }
                }
                if (ok && !handled && !strcmp(line, "input"))
                {
                    handled = 1;
                    ok = descriptor->input_count < TP_WORKLOAD_MAX_INPUTS &&
                         tp_workload_input_parse(value, descriptor->inputs + descriptor->input_count);
                    if (ok) ++descriptor->input_count;
                }
                if (ok && !handled && (!strcmp(line, "compile_argv") || !strcmp(line, "link_argv")))
                {
                    handled = 1;
                    char (*arguments)[TP_WORKLOAD_TEXT_CAP] = !strcmp(line, "compile_argv") ? descriptor->compile_arguments : descriptor->link_arguments;
                    unsigned* count = !strcmp(line, "compile_argv") ? &descriptor->compile_argument_count : &descriptor->link_argument_count;
                    ok = *count < TP_WORKLOAD_MAX_ARGUMENTS && tp_workload_text_copy(arguments[*count], TP_WORKLOAD_TEXT_CAP, value);
                    if (ok) ++*count;
                }
                ok = ok && handled;
            }
        }
        if (!ok) tp_error("malformed workload descriptor %s at line %u", path, line_number);
    }
    if (file)
    {
        ok = ok && !ferror(file);
        if (fclose(file) != 0) ok = 0;
    }
    ok = ok && seen == required && descriptor->input_count > 0 &&
         !strcmp(descriptor->schema, TP_WORKLOAD_DESCRIPTOR_SCHEMA) &&
         !strcmp(descriptor->admission, "fresh-required") && !strcmp(descriptor->cwd, ".") &&
         (!strcmp(descriptor->historical_outcome, "pass") || !strcmp(descriptor->historical_outcome, "failed") ||
          !strcmp(descriptor->historical_outcome, "inconclusive")) &&
         tp_workload_sha256(descriptor->input_tree_sha256) &&
         tp_workload_u64(requested_bytes, &descriptor->requested_translation_unit_bytes) && descriptor->requested_translation_unit_bytes > 0 &&
         tp_workload_arguments_valid(descriptor->compile_arguments, descriptor->compile_argument_count, 0) &&
         tp_workload_arguments_valid(descriptor->link_arguments, descriptor->link_argument_count, 1);
    uint64_t requested = 0;
    for (unsigned i = 0; i < descriptor->input_count && ok; ++i)
    {
        for (unsigned j = 0; j < i && ok; ++j) ok = strcmp(descriptor->inputs[i].path, descriptor->inputs[j].path) != 0;
        if (!strcmp(descriptor->inputs[i].role, "source") || !strcmp(descriptor->inputs[i].role, "generated"))
        {
            uint64_t bytes = descriptor->inputs[i].bytes;
            ok = requested <= UINT64_MAX - bytes;
            if (ok) requested += bytes;
        }
    }
    ok = ok && requested == descriptor->requested_translation_unit_bytes;
    if (!ok && !line_number) tp_error("cannot read workload descriptor %s", path);
    else if (!ok) tp_error("incomplete or inconsistent workload descriptor %s", path);
    return ok;
}

static int tp_workload_regular_file(char const* path)
{
    int ok = 0;
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    ok = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat status;
    ok = lstat(path, &status) == 0 && S_ISREG(status.st_mode);
#endif
    return ok;
}

static int tp_workload_directory(char const* path)
{
    int ok = 0;
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    ok = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
         !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat status;
    ok = lstat(path, &status) == 0 && S_ISDIR(status.st_mode);
#endif
    return ok;
}

static int tp_workload_descriptor_inputs(char const* root, TpWorkloadDescriptor const* descriptor,
                                         char actual_tree_sha256[65])
{
    int ok = 1;
    for (unsigned i = 0; i < descriptor->input_count && ok; ++i)
    {
        char path[TP_PATH_CAP], sha256[65];
        uint64_t bytes = 0, lines = 0;
        TpWorkloadInput const* input = descriptor->inputs + i;
        ok = tp_path(path, root, input->path) && tp_workload_regular_file(path) &&
             tp_hash_file(path, sha256, &bytes, &lines) && bytes == input->bytes && !strcmp(sha256, input->sha256);
        if (!ok) tp_error("workload input missing, unsafe, or changed: %s", input->path);
    }
    if (ok) ok = tp_hash_tree(root, actual_tree_sha256) && !strcmp(actual_tree_sha256, descriptor->input_tree_sha256);
    if (!ok) tp_error("workload input inventory changed or contains undeclared files: %s", root);
    return ok;
}

static void tp_workload_json_arguments(FILE* file, char const* key,
                                       char arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP], unsigned count)
{
    fputs(",\"", file);
    fputs(key, file);
    fputs("\":[", file);
    for (unsigned i = 0; i < count; ++i)
    {
        if (i) fputc(',', file);
        tp_json_string(file, arguments[i]);
    }
    fputc(']', file);
}

static int tp_workload_check(TpWorkloadCheckOptions options)
{
    TpWorkloadDescriptor descriptor;
    char descriptor_path[TP_PATH_CAP], root[TP_PATH_CAP], compiler[TP_PATH_CAP], evidence[TP_PATH_CAP];
    char descriptor_sha256[65], tree_sha256[65], compiler_sha256[65], evidence_sha256[65];
    uint64_t descriptor_bytes = 0, compiler_bytes = 0, evidence_bytes = 0, lines = 0;
    int outcome = options.evidence_outcome &&
                  (!strcmp(options.evidence_outcome, "pass") || !strcmp(options.evidence_outcome, "failed") ||
                   !strcmp(options.evidence_outcome, "inconclusive") || !strcmp(options.evidence_outcome, "unavailable"));
    int ok = options.descriptor && options.source_root && options.compiler && options.evidence && outcome &&
             tp_absolute(options.descriptor, descriptor_path) && tp_absolute(options.source_root, root) &&
             tp_absolute(options.compiler, compiler) && tp_absolute(options.evidence, evidence) &&
             tp_workload_regular_file(descriptor_path) && tp_workload_directory(root) &&
             tp_workload_regular_file(compiler) && tp_workload_regular_file(evidence) &&
             tp_workload_descriptor_parse(descriptor_path, &descriptor) &&
             tp_hash_file(descriptor_path, descriptor_sha256, &descriptor_bytes, &lines) &&
             tp_hash_file(compiler, compiler_sha256, &compiler_bytes, &lines) &&
             tp_hash_file(evidence, evidence_sha256, &evidence_bytes, &lines) &&
             tp_workload_descriptor_inputs(root, &descriptor, tree_sha256);
    if (ok)
    {
        fputs("{\"schema\":\"buster-throughput-workload-preflight-v1\",\"scope\":\"descriptor-and-input-preflight\",", stdout);
        fputs("\"admitted\":false,\"performed_work\":null,\"fresh_admission_required\":true,\"name\":", stdout);
        tp_json_string(stdout, descriptor.name);
        fputs(",\"family\":", stdout); tp_json_string(stdout, descriptor.family);
        fputs(",\"source_identity\":", stdout); tp_json_string(stdout, descriptor.source_identity);
        fputs(",\"dependency_identity\":", stdout); tp_json_string(stdout, descriptor.dependency_identity);
        fputs(",\"generated_identity\":", stdout); tp_json_string(stdout, descriptor.generated_identity);
        fputs(",\"resource_identity\":", stdout); tp_json_string(stdout, descriptor.resource_identity);
        fputs(",\"sysroot_identity\":", stdout); tp_json_string(stdout, descriptor.sysroot_identity);
        fputs(",\"sdk_identity\":", stdout); tp_json_string(stdout, descriptor.sdk_identity);
        fputs(",\"environment_identity\":", stdout); tp_json_string(stdout, descriptor.environment_identity);
        fputs(",\"target\":", stdout); tp_json_string(stdout, descriptor.target);
        fputs(",\"abi\":", stdout); tp_json_string(stdout, descriptor.abi);
        fputs(",\"cpu_features\":", stdout); tp_json_string(stdout, descriptor.cpu_features);
        fputs(",\"c_lowerings\":", stdout); tp_json_string(stdout, descriptor.c_lowerings);
        fputs(",\"pic_modes\":", stdout); tp_json_string(stdout, descriptor.pic_modes);
        fputs(",\"allocator_modes\":", stdout); tp_json_string(stdout, descriptor.allocator_modes);
        fputs(",\"operations\":", stdout); tp_json_string(stdout, descriptor.operations);
        fputs(",\"artifacts\":", stdout); tp_json_string(stdout, descriptor.artifacts);
        fputs(",\"oracle\":", stdout); tp_json_string(stdout, descriptor.oracle);
        fputs(",\"historical_outcome\":", stdout); tp_json_string(stdout, descriptor.historical_outcome);
        fputs(",\"historical_evidence\":", stdout); tp_json_string(stdout, descriptor.historical_evidence);
        fputs(",\"admission\":", stdout); tp_json_string(stdout, descriptor.admission);
        fputs(",\"cwd\":", stdout); tp_json_string(stdout, descriptor.cwd);
        fputs(",\"oracle_evidence_outcome\":", stdout); tp_json_string(stdout, options.evidence_outcome);
        tp_workload_json_arguments(stdout, "compile_argv", descriptor.compile_arguments, descriptor.compile_argument_count);
        tp_workload_json_arguments(stdout, "link_argv", descriptor.link_arguments, descriptor.link_argument_count);
        fprintf(stdout, ",\"input_count\":%u,\"requested_translation_unit_bytes\":%" PRIu64, descriptor.input_count,
                descriptor.requested_translation_unit_bytes);
        fputs(",\"input_tree_sha256\":", stdout); tp_json_string(stdout, tree_sha256);
        fputs(",\"descriptor\":{\"path\":", stdout); tp_json_string(stdout, descriptor_path);
        fputs(",\"sha256\":", stdout); tp_json_string(stdout, descriptor_sha256);
        fprintf(stdout, ",\"bytes\":%" PRIu64 "}", descriptor_bytes);
        fputs(",\"compiler\":{\"path\":", stdout); tp_json_string(stdout, compiler);
        fputs(",\"sha256\":", stdout); tp_json_string(stdout, compiler_sha256);
        fprintf(stdout, ",\"bytes\":%" PRIu64 "}", compiler_bytes);
        fputs(",\"oracle_evidence\":{\"path\":", stdout); tp_json_string(stdout, evidence);
        fputs(",\"sha256\":", stdout); tp_json_string(stdout, evidence_sha256);
        fprintf(stdout, ",\"bytes\":%" PRIu64 "}}\n", evidence_bytes);
        ok = !ferror(stdout) && fflush(stdout) == 0;
    }
    if (!ok) tp_error("workload preflight invalid; no workload admission claimed");
    return ok;
}

#endif
