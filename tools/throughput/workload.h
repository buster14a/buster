/* Real-source workload qualification for the native throughput tool.
 * Ownership: this file parses a bounded, line-oriented descriptor, verifies
 * its staged input inventory and performs one exact functional admission.
 * Map: tp_workload_descriptor_parse (syntax and schema),
 * tp_workload_descriptor_inputs (content/inventory closure),
 * tp_workload_check (read-only JSON preflight report),
 * tp_workload_admit (compile, link, runtime and immutable admission receipt).
 */
#ifndef BUSTER_THROUGHPUT_WORKLOAD_H
#define BUSTER_THROUGHPUT_WORKLOAD_H

#define TP_WORKLOAD_DESCRIPTOR_SCHEMA_V1 "buster-throughput-workload-v1"
#define TP_WORKLOAD_DESCRIPTOR_SCHEMA "buster-throughput-workload-v2"
#define TP_WORKLOAD_MAX_INPUTS 96
#define TP_WORKLOAD_MAX_ARGUMENTS 64
#define TP_WORKLOAD_MAX_EXPANDED_ARGUMENTS 160
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
    char runtime_identity[256];
    char target[128];
    char abi[128];
    char cpu[128];
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
    char admission_frontend[64];
    char admission_pic[32];
    char admission_allocator[32];
    char oracle_success[TP_WORKLOAD_TEXT_CAP];
    char runtime_transcript_sha256[65];
    char cwd[TP_WORKLOAD_TEXT_CAP];
    char input_tree_sha256[65];
    uint64_t requested_translation_unit_bytes;
    TpWorkloadInput inputs[TP_WORKLOAD_MAX_INPUTS];
    unsigned input_count;
    char object_arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP];
    unsigned object_argument_count;
    char compile_link_arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP];
    unsigned compile_link_argument_count;
    char runtime_arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP];
    unsigned runtime_argument_count;
    char legacy_compile_arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP];
    unsigned legacy_compile_argument_count;
    char legacy_link_arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP];
    unsigned legacy_link_argument_count;
    char link_inputs[TP_WORKLOAD_MAX_INPUTS][TP_WORKLOAD_TEXT_CAP];
    unsigned link_input_count;
} TpWorkloadDescriptor;

typedef struct TpWorkloadCheckOptions
{
    char const* descriptor;
    char const* source_root;
    char const* compiler;
    char const* evidence;
    char const* evidence_outcome;
} TpWorkloadCheckOptions;

typedef struct TpWorkloadAdmitOptions
{
    TpWorkloadCheckOptions check;
    char const* output;
    char const* qualification_id;
    char const* dependency_manifest;
    char const* resource_manifest;
    char const* sysroot_manifest;
    char const* sdk_manifest;
    char const* environment_manifest;
    char const* runtime_manifest;
} TpWorkloadAdmitOptions;

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

static int tp_workload_csv_contains(char const* values, char const* expected)
{
    size_t expected_length = strlen(expected);
    int found = 0;
    for (char const* start = values; *start && !found;)
    {
        char const* end = strchr(start, ',');
        size_t length = end ? (size_t)(end - start) : strlen(start);
        found = length == expected_length && !memcmp(start, expected, length);
        start = end ? end + 1 : start + length;
    }
    return found;
}

static int tp_workload_arguments_valid(char arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP],
                                       unsigned count, int compile_link, int legacy)
{
    unsigned compiler = 0, output = 0, output_option = 0, operation = 0, source = 0;
    unsigned mode = 0, frontend = 0, pic = 0, metrics = 0, link_sources = 0, objects = 0, root = 0, target = 0, cpu = 0;
    int ok = count >= 2 && !strcmp(arguments[0], "$COMPILER") && !strcmp(arguments[1], "cc");
    for (unsigned i = 0; i < count && ok; ++i)
    {
        char const* value = arguments[i];
        int known_placeholder = !strcmp(value, "$COMPILER") || !strcmp(value, "$OUTPUT") ||
                                !strcmp(value, "$FRONTEND") || !strcmp(value, "$PIC") ||
                                !strcmp(value, "$TARGET") || !strcmp(value, "$CPU") ||
                                !strcmp(value, "-I$ROOT") || !strcmp(value, "-fregister-allocator=$MODE") ||
                                !strcmp(value, "-fsource-metrics=$METRICS") ||
                                (!compile_link && !strcmp(value, "$SOURCE")) ||
                                (compile_link && !strcmp(value, "$LINK_SOURCES")) || (legacy && compile_link && !strcmp(value, "$OBJECTS"));
        ok = value[0] && !strchr(value, '\t') && (known_placeholder || !strchr(value, '$'));
        compiler += !strcmp(value, "$COMPILER");
        output += !strcmp(value, "$OUTPUT");
        output_option += !strcmp(value, "-o");
        operation += !strcmp(value, compile_link ? "cc" : "-c");
        source += !strcmp(value, "$SOURCE");
        mode += !strcmp(value, "-fregister-allocator=$MODE");
        frontend += !strcmp(value, "$FRONTEND");
        pic += !strcmp(value, "$PIC");
        target += !strcmp(value, "$TARGET");
        cpu += !strcmp(value, "$CPU");
        metrics += !strcmp(value, "-fsource-metrics=$METRICS");
        link_sources += !strcmp(value, "$LINK_SOURCES");
        objects += !strcmp(value, "$OBJECTS");
        root += !strcmp(value, "-I$ROOT");
    }
    unsigned output_index = count;
    for (unsigned i = 0; i < count; ++i) if (!strcmp(arguments[i], "-o")) output_index = i;
    ok = ok && output_option == 1 && output_index + 1 < count && !strcmp(arguments[output_index + 1], "$OUTPUT");
    if (legacy && compile_link)
        ok = ok && compiler == 1 && output == 1 && operation == 1 && objects == 1 && !link_sources && !source && !mode && !frontend && !pic &&
             !target && !cpu && !metrics && !root;
    else if (legacy)
        ok = ok && compiler == 1 && output == 1 && operation == 1 && source == 1 && !objects && !link_sources && mode == 1 && frontend == 1 && pic == 1 &&
             !target && !cpu && metrics == 1 && root == 1;
    else if (compile_link)
        ok = ok && compiler == 1 && output == 1 && operation == 1 && link_sources == 1 && !source && mode == 1 && frontend == 1 && pic == 1 &&
             target == 1 && cpu == 1 && metrics == 1 && root == 1;
    else
        ok = ok && compiler == 1 && output == 1 && operation == 1 && source == 1 && !link_sources && mode == 1 && frontend == 1 && pic == 1 &&
             target == 1 && cpu == 1 && metrics == 1 && root == 1;
    return ok;
}

static int tp_workload_runtime_arguments_valid(char arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP], unsigned count)
{
    unsigned executable = 0;
    int ok = count > 0 && !strcmp(arguments[0], "$EXECUTABLE");
    for (unsigned i = 0; i < count && ok; ++i)
    {
        char const* value = arguments[i];
        int known_placeholder = !strcmp(value, "$EXECUTABLE");
        ok = value[0] && !strchr(value, '\t') && (known_placeholder || !strchr(value, '$'));
        executable += known_placeholder;
    }
    ok = ok && executable == 1;
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
        {"cpu", descriptor->cpu, sizeof(descriptor->cpu), UINT64_C(1) << 12},
        {"cpu_features", descriptor->cpu_features, sizeof(descriptor->cpu_features), UINT64_C(1) << 13},
        {"c_lowerings", descriptor->c_lowerings, sizeof(descriptor->c_lowerings), UINT64_C(1) << 14},
        {"pic_modes", descriptor->pic_modes, sizeof(descriptor->pic_modes), UINT64_C(1) << 15},
        {"allocator_modes", descriptor->allocator_modes, sizeof(descriptor->allocator_modes), UINT64_C(1) << 16},
        {"operations", descriptor->operations, sizeof(descriptor->operations), UINT64_C(1) << 17},
        {"artifacts", descriptor->artifacts, sizeof(descriptor->artifacts), UINT64_C(1) << 18},
        {"oracle", descriptor->oracle, sizeof(descriptor->oracle), UINT64_C(1) << 19},
        {"oracle_success", descriptor->oracle_success, sizeof(descriptor->oracle_success), UINT64_C(1) << 20},
        {"historical_outcome", descriptor->historical_outcome, sizeof(descriptor->historical_outcome), UINT64_C(1) << 21},
        {"historical_evidence", descriptor->historical_evidence, sizeof(descriptor->historical_evidence), UINT64_C(1) << 22},
        {"admission", descriptor->admission, sizeof(descriptor->admission), UINT64_C(1) << 23},
        {"admission_frontend", descriptor->admission_frontend, sizeof(descriptor->admission_frontend), UINT64_C(1) << 24},
        {"admission_pic", descriptor->admission_pic, sizeof(descriptor->admission_pic), UINT64_C(1) << 25},
        {"admission_allocator", descriptor->admission_allocator, sizeof(descriptor->admission_allocator), UINT64_C(1) << 26},
        {"runtime_transcript_sha256", descriptor->runtime_transcript_sha256, sizeof(descriptor->runtime_transcript_sha256), UINT64_C(1) << 27},
        {"cwd", descriptor->cwd, sizeof(descriptor->cwd), UINT64_C(1) << 28},
        {"requested_translation_unit_bytes", requested_bytes, sizeof(requested_bytes), UINT64_C(1) << 29},
        {"input_tree_sha256", descriptor->input_tree_sha256, sizeof(descriptor->input_tree_sha256), UINT64_C(1) << 30},
        {"runtime_identity", descriptor->runtime_identity, sizeof(descriptor->runtime_identity), UINT64_C(1) << 31},
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
                if (ok && !handled && (!strcmp(line, "object_argv") || !strcmp(line, "compile_link_argv") || !strcmp(line, "runtime_argv") ||
                                       !strcmp(line, "compile_argv") || !strcmp(line, "link_argv")))
                {
                    handled = 1;
                    char (*arguments)[TP_WORKLOAD_TEXT_CAP] = !strcmp(line, "object_argv") ? descriptor->object_arguments :
                        !strcmp(line, "compile_link_argv") ? descriptor->compile_link_arguments :
                        !strcmp(line, "runtime_argv") ? descriptor->runtime_arguments :
                        !strcmp(line, "compile_argv") ? descriptor->legacy_compile_arguments : descriptor->legacy_link_arguments;
                    unsigned* count = !strcmp(line, "object_argv") ? &descriptor->object_argument_count :
                        !strcmp(line, "compile_link_argv") ? &descriptor->compile_link_argument_count :
                        !strcmp(line, "runtime_argv") ? &descriptor->runtime_argument_count :
                        !strcmp(line, "compile_argv") ? &descriptor->legacy_compile_argument_count : &descriptor->legacy_link_argument_count;
                    ok = *count < TP_WORKLOAD_MAX_ARGUMENTS && tp_workload_text_copy(arguments[*count], TP_WORKLOAD_TEXT_CAP, value);
                    if (ok) ++*count;
                }
                if (ok && !handled && !strcmp(line, "link_input"))
                {
                    handled = 1;
                    ok = descriptor->link_input_count < TP_WORKLOAD_MAX_INPUTS && tp_workload_relative_path(value) &&
                         tp_workload_text_copy(descriptor->link_inputs[descriptor->link_input_count], TP_WORKLOAD_TEXT_CAP, value);
                    if (ok) ++descriptor->link_input_count;
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
    uint64_t legacy_required = ((UINT64_C(1) << 12) - 1) |
        (UINT64_C(1) << 13) | (UINT64_C(1) << 14) | (UINT64_C(1) << 15) | (UINT64_C(1) << 16) |
        (UINT64_C(1) << 17) | (UINT64_C(1) << 18) | (UINT64_C(1) << 19) | (UINT64_C(1) << 21) |
        (UINT64_C(1) << 22) | (UINT64_C(1) << 23) | (UINT64_C(1) << 28) | (UINT64_C(1) << 29) | (UINT64_C(1) << 30);
    int legacy = !strcmp(descriptor->schema, TP_WORKLOAD_DESCRIPTOR_SCHEMA_V1);
    ok = ok && descriptor->input_count > 0 && ((legacy && (seen & legacy_required) == legacy_required) || (!legacy && seen == required)) &&
         (legacy || !strcmp(descriptor->schema, TP_WORKLOAD_DESCRIPTOR_SCHEMA)) &&
         !strcmp(descriptor->admission, "fresh-required") && !strcmp(descriptor->cwd, ".") &&
         (!strcmp(descriptor->historical_outcome, "pass") || !strcmp(descriptor->historical_outcome, "failed") ||
          !strcmp(descriptor->historical_outcome, "inconclusive")) &&
         tp_workload_sha256(descriptor->input_tree_sha256) &&
         tp_workload_u64(requested_bytes, &descriptor->requested_translation_unit_bytes) && descriptor->requested_translation_unit_bytes > 0;
    if (legacy)
        ok = ok && tp_workload_arguments_valid(descriptor->legacy_compile_arguments, descriptor->legacy_compile_argument_count, 0, 1) &&
             tp_workload_arguments_valid(descriptor->legacy_link_arguments, descriptor->legacy_link_argument_count, 1, 1);
    else
        ok = ok && !strcmp(descriptor->operations, "source-to-object,source-to-linked-executable,runtime") &&
             !strcmp(descriptor->artifacts, "object,executable,runtime-transcript") &&
             tp_workload_csv_contains(descriptor->c_lowerings, descriptor->admission_frontend) &&
             tp_workload_csv_contains(descriptor->pic_modes, descriptor->admission_pic) &&
             tp_workload_csv_contains(descriptor->allocator_modes, descriptor->admission_allocator) &&
             !strcmp(descriptor->target, "x86_64-unknown-linux-gnu") && !strcmp(descriptor->abi, "sysv-amd64") &&
             !strcmp(descriptor->cpu, "baseline") && !strcmp(descriptor->cpu_features, "baseline") &&
             !strcmp(descriptor->admission_frontend, "direct-ssa") && !strcmp(descriptor->admission_pic, "off") &&
             !strcmp(descriptor->admission_allocator, "fast") && tp_workload_sha256(descriptor->runtime_transcript_sha256) &&
             tp_workload_arguments_valid(descriptor->object_arguments, descriptor->object_argument_count, 0, 0) &&
             tp_workload_arguments_valid(descriptor->compile_link_arguments, descriptor->compile_link_argument_count, 1, 0) &&
             tp_workload_runtime_arguments_valid(descriptor->runtime_arguments, descriptor->runtime_argument_count) && descriptor->link_input_count > 0;
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
    for (unsigned i = 0; i < descriptor->link_input_count && ok; ++i)
    {
        int matched = 0;
        for (unsigned j = 0; j < descriptor->input_count; ++j)
            matched |= (!strcmp(descriptor->inputs[j].role, "source") || !strcmp(descriptor->inputs[j].role, "generated")) &&
                       !strcmp(descriptor->link_inputs[i], descriptor->inputs[j].path);
        for (unsigned j = 0; j < i && ok; ++j) ok = strcmp(descriptor->link_inputs[i], descriptor->link_inputs[j]) != 0;
        ok = ok && matched;
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
        int legacy = !strcmp(descriptor.schema, TP_WORKLOAD_DESCRIPTOR_SCHEMA_V1);
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
        if (!legacy) { fputs(",\"runtime_identity\":", stdout); tp_json_string(stdout, descriptor.runtime_identity); }
        fputs(",\"target\":", stdout); tp_json_string(stdout, descriptor.target);
        fputs(",\"abi\":", stdout); tp_json_string(stdout, descriptor.abi);
        if (!legacy) { fputs(",\"cpu\":", stdout); tp_json_string(stdout, descriptor.cpu); }
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
        if (!legacy)
        {
            fputs(",\"admission_frontend\":", stdout); tp_json_string(stdout, descriptor.admission_frontend);
            fputs(",\"admission_pic\":", stdout); tp_json_string(stdout, descriptor.admission_pic);
            fputs(",\"admission_allocator\":", stdout); tp_json_string(stdout, descriptor.admission_allocator);
        }
        fputs(",\"cwd\":", stdout); tp_json_string(stdout, descriptor.cwd);
        fputs(",\"oracle_evidence_outcome\":", stdout); tp_json_string(stdout, options.evidence_outcome);
        if (legacy)
        {
            tp_workload_json_arguments(stdout, "compile_argv", descriptor.legacy_compile_arguments, descriptor.legacy_compile_argument_count);
            tp_workload_json_arguments(stdout, "link_argv", descriptor.legacy_link_arguments, descriptor.legacy_link_argument_count);
        }
        else
        {
            tp_workload_json_arguments(stdout, "object_argv", descriptor.object_arguments, descriptor.object_argument_count);
            tp_workload_json_arguments(stdout, "compile_link_argv", descriptor.compile_link_arguments, descriptor.compile_link_argument_count);
            tp_workload_json_arguments(stdout, "runtime_argv", descriptor.runtime_arguments, descriptor.runtime_argument_count);
            fputs(",\"runtime_transcript_sha256\":", stdout); tp_json_string(stdout, descriptor.runtime_transcript_sha256);
            fputs(",\"link_inputs\":[", stdout);
            for (unsigned i = 0; i < descriptor.link_input_count; ++i)
            {
                if (i) fputc(',', stdout);
                tp_json_string(stdout, descriptor.link_inputs[i]);
            }
            fputc(']', stdout);
        }
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

typedef struct TpWorkloadIdentity
{
    char path[TP_PATH_CAP];
    char sha256[65];
    char identity[TP_WORKLOAD_TEXT_CAP];
    char operation[64];
    uint64_t bytes;
    unsigned file_count;
    unsigned tree_count;
    unsigned fact_count;
    char arguments[16][TP_WORKLOAD_TEXT_CAP];
    char argument_operations[16][64];
    unsigned argument_count;
    uint32_t bound_arguments;
    uint32_t bound_operations;
} TpWorkloadIdentity;

typedef struct TpWorkloadArtifact
{
    char role[32];
    char source[TP_WORKLOAD_TEXT_CAP];
    char path[TP_PATH_CAP];
    char sha256[65];
    uint64_t bytes;
    char metrics_path[TP_PATH_CAP];
    char metrics_sha256[65];
    uint64_t metrics_bytes;
    uint64_t translated_bytes;
    uint64_t translated_lines;
} TpWorkloadArtifact;

typedef struct TpWorkloadExpandedArguments
{
    char* values[TP_WORKLOAD_MAX_EXPANDED_ARGUMENTS + 1];
    char include[TP_PATH_CAP + 3];
    char target[256];
    char cpu[256];
    char mode[128];
    char metrics[TP_PATH_CAP + 32];
    char frontend[64];
    char pic[32];
    char link_sources[TP_WORKLOAD_MAX_INPUTS][TP_PATH_CAP];
    unsigned count;
} TpWorkloadExpandedArguments;

static int tp_workload_file_contains_line(char const* path, char const* needle)
{
    FILE* file = fopen(path, "rb");
    int found = 0, failed = file == NULL;
    char line[4096];
    while (!failed && !found && fgets(line, sizeof(line), file))
    {
        size_t length = strlen(line);
        if (!length || line[length - 1] != '\n') failed = 1;
        else
        {
            line[--length] = 0;
            if (length && line[length - 1] == '\r') line[--length] = 0;
            found = !strcmp(line, needle);
        }
    }
    if (file && ferror(file)) failed = 1;
    if (file && fclose(file) != 0) failed = 1;
    return found && !failed;
}

static int tp_workload_identity_file(char* value)
{
    char* hash = strchr(value, '\t');
    char* bytes_text = hash ? strchr(hash + 1, '\t') : NULL;
    uint64_t expected_bytes = 0, actual_bytes = 0, lines = 0;
    char actual_sha256[65];
    int ok = hash && bytes_text;
    if (ok)
    {
        *hash++ = 0;
        *bytes_text++ = 0;
        ok = value[0] == '/' && tp_workload_sha256(hash) && tp_workload_u64(bytes_text, &expected_bytes) &&
             tp_workload_regular_file(value) && tp_hash_file(value, actual_sha256, &actual_bytes, &lines) &&
             actual_bytes == expected_bytes && !strcmp(actual_sha256, hash);
    }
    return ok;
}

static int tp_workload_identity_tree(char* value)
{
    char* hash = strchr(value, '\t');
    char actual_sha256[65];
    int ok = hash != NULL;
    if (ok)
    {
        *hash++ = 0;
        ok = value[0] == '/' && tp_workload_sha256(hash) && tp_workload_directory(value) &&
             tp_hash_tree(value, actual_sha256) && !strcmp(actual_sha256, hash);
    }
    return ok;
}

static int tp_workload_identity_fact(char* value, char const* expected_kind)
{
    int ok = value[0] && strlen(value) < TP_WORKLOAD_TEXT_CAP;
    if (ok && !strcmp(expected_kind, "environment"))
    {
        char* assignment = !strncmp(value, "env:", 4) ? value + 4 : NULL;
        char* equal = assignment ? strchr(assignment, '=') : NULL;
        if (equal) *equal++ = 0;
        char const* actual = equal ? getenv(assignment) : NULL;
        ok = actual && !strcmp(actual, equal);
    }
    if (ok && !strcmp(expected_kind, "sdk"))
    {
#ifdef __linux__
        ok = !strcmp(value, "not-applicable-linux");
#else
        ok = 0;
#endif
    }
    return ok;
}

static int tp_workload_identity(char const* path, char const* expected_kind, TpWorkloadIdentity* identity)
{
    memset(identity, 0, sizeof(*identity));
    uint64_t lines = 0;
    int ok = path && tp_absolute(path, identity->path) && tp_workload_regular_file(identity->path) &&
             tp_hash_file(identity->path, identity->sha256, &identity->bytes, &lines) && identity->bytes > 0;
    FILE* file = ok ? fopen(identity->path, "rb") : NULL;
    char line[4096], schema[64] = {0}, kind[64] = {0};
    unsigned line_number = 0, schema_count = 0, kind_count = 0, identity_count = 0, operation_count = 0;
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
                if (!strcmp(line, "schema")) ok = ++schema_count == 1 && tp_workload_text_copy(schema, sizeof(schema), value);
                else if (!strcmp(line, "kind")) ok = ++kind_count == 1 && tp_workload_text_copy(kind, sizeof(kind), value);
                else if (!strcmp(line, "identity"))
                    ok = ++identity_count == 1 && tp_workload_text_copy(identity->identity, sizeof(identity->identity), value);
                else if (!strcmp(line, "operation"))
                    ok = ++operation_count == 1 && tp_workload_text_copy(identity->operation, sizeof(identity->operation), value);
                else if (!strcmp(line, "argument"))
                {
                    char* separator = strchr(value, '|');
                    if (separator) *separator++ = 0;
                    ok = separator && identity->argument_count < BUSTER_ARRAY_LENGTH(identity->arguments) &&
                         tp_workload_text_copy(identity->argument_operations[identity->argument_count],
                                               sizeof(identity->argument_operations[identity->argument_count]), value) &&
                         tp_workload_text_copy(identity->arguments[identity->argument_count], TP_WORKLOAD_TEXT_CAP, separator);
                    if (ok) ++identity->argument_count;
                }
                else if (!strcmp(line, "file")) { ok = tp_workload_identity_file(value); identity->file_count += ok; }
                else if (!strcmp(line, "tree")) { ok = tp_workload_identity_tree(value); identity->tree_count += ok; }
                else if (!strcmp(line, "fact")) { ok = tp_workload_identity_fact(value, expected_kind); identity->fact_count += ok; }
                else ok = 0;
            }
        }
    }
    if (file)
    {
        ok = ok && !ferror(file);
        if (fclose(file) != 0) ok = 0;
    }
    ok = ok && schema_count == 1 && kind_count == 1 && identity_count == 1 && operation_count == 1 &&
         !strcmp(schema, "buster-throughput-identity-v1") && !strcmp(kind, expected_kind) &&
         tp_workload_csv_contains(identity->operation, "source-to-object") +
             tp_workload_csv_contains(identity->operation, "source-to-linked-executable") +
             tp_workload_csv_contains(identity->operation, "runtime") > 0 &&
         (!strcmp(identity->operation, "source-to-object") || !strcmp(identity->operation, "source-to-linked-executable") ||
          !strcmp(identity->operation, "runtime") || !strcmp(identity->operation, "source-to-object,source-to-linked-executable") ||
          !strcmp(identity->operation, "source-to-object,runtime") ||
          !strcmp(identity->operation, "source-to-linked-executable,runtime") ||
          !strcmp(identity->operation, "source-to-object,source-to-linked-executable,runtime")) &&
         identity->file_count + identity->tree_count + identity->fact_count > 0;
    for (unsigned i = 0; i < identity->argument_count && ok; ++i)
        ok = tp_workload_csv_contains(identity->operation, identity->argument_operations[i]);
    if (!ok) tp_error("invalid or drifted %s identity manifest at line %u: %s", expected_kind, line_number, path ? path : "(null)");
    return ok;
}

static int tp_workload_source_input(TpWorkloadInput const* input)
{
    return !strcmp(input->role, "source") || !strcmp(input->role, "generated");
}

static char const* tp_workload_frontend_flag(char const* frontend)
{
    char const* flag = NULL;
    if (!strcmp(frontend, "direct-ssa")) flag = "-ffrontend-ssa";
    else if (!strcmp(frontend, "local-backed-canonical")) flag = "-fno-frontend-ssa";
    return flag;
}

static char const* tp_workload_pic_flag(char const* pic)
{
    char const* flag = NULL;
    if (!strcmp(pic, "off")) flag = "-fno-pic";
    else if (!strcmp(pic, "on")) flag = "-fPIC";
    return flag;
}

static int tp_workload_expand_arguments(TpWorkloadDescriptor const* descriptor, char const* compiler, char const* root,
                                        char arguments[TP_WORKLOAD_MAX_ARGUMENTS][TP_WORKLOAD_TEXT_CAP], unsigned argument_count,
                                        char const* source, char const* output, char const* metrics, char const* executable,
                                        TpWorkloadExpandedArguments* expanded)
{
    memset(expanded, 0, sizeof(*expanded));
    snprintf(expanded->include, sizeof(expanded->include), "-I%s", root);
    snprintf(expanded->target, sizeof(expanded->target), "--target=%s", descriptor->target);
    snprintf(expanded->cpu, sizeof(expanded->cpu), "-mcpu=%s", descriptor->cpu);
    snprintf(expanded->mode, sizeof(expanded->mode), "-fregister-allocator=%s", descriptor->admission_allocator);
    snprintf(expanded->metrics, sizeof(expanded->metrics), "-fsource-metrics=%s", metrics ? metrics : "");
    char const* frontend = tp_workload_frontend_flag(descriptor->admission_frontend);
    char const* pic = tp_workload_pic_flag(descriptor->admission_pic);
    int ok = frontend && pic && strlen(frontend) < sizeof(expanded->frontend) && strlen(pic) < sizeof(expanded->pic) &&
             strlen(expanded->include) < sizeof(expanded->include) - 1 && strlen(expanded->target) < sizeof(expanded->target) - 1 &&
             strlen(expanded->cpu) < sizeof(expanded->cpu) - 1 && strlen(expanded->mode) < sizeof(expanded->mode) - 1 &&
             strlen(expanded->metrics) < sizeof(expanded->metrics) - 1;
    if (ok)
    {
        strcpy(expanded->frontend, frontend);
        strcpy(expanded->pic, pic);
    }
    for (unsigned i = 0; i < argument_count && ok; ++i)
    {
        char const* value = arguments[i];
        if (!strcmp(value, "$LINK_SOURCES"))
        {
            for (unsigned j = 0; j < descriptor->link_input_count && ok; ++j)
            {
                ok = expanded->count < TP_WORKLOAD_MAX_EXPANDED_ARGUMENTS &&
                     tp_path(expanded->link_sources[j], root, descriptor->link_inputs[j]);
                if (ok) expanded->values[expanded->count++] = expanded->link_sources[j];
            }
        }
        else
        {
            char const* replacement = !strcmp(value, "$COMPILER") ? compiler :
                !strcmp(value, "$OUTPUT") ? output : !strcmp(value, "$SOURCE") ? source :
                !strcmp(value, "$FRONTEND") ? expanded->frontend : !strcmp(value, "$PIC") ? expanded->pic :
                !strcmp(value, "$TARGET") ? expanded->target : !strcmp(value, "$CPU") ? expanded->cpu :
                !strcmp(value, "$EXECUTABLE") ? executable :
                !strcmp(value, "-I$ROOT") ? expanded->include :
                !strcmp(value, "-fregister-allocator=$MODE") ? expanded->mode :
                !strcmp(value, "-fsource-metrics=$METRICS") ? expanded->metrics : value;
            ok = replacement && expanded->count < TP_WORKLOAD_MAX_EXPANDED_ARGUMENTS;
            if (ok) expanded->values[expanded->count++] = (char*)replacement;
        }
    }
    expanded->values[expanded->count] = NULL;
    return ok;
}

static int tp_workload_bind_identities(TpWorkloadIdentity identities[6], char const* operation,
                                       TpWorkloadExpandedArguments const* expanded, char const* executable)
{
    int ok = 1;
    for (unsigned i = 0; i < 6 && ok; ++i)
    {
        if (!tp_workload_csv_contains(identities[i].operation, operation)) continue;
        uint32_t operation_bit = !strcmp(operation, "source-to-object") ? UINT32_C(1) :
            !strcmp(operation, "source-to-linked-executable") ? UINT32_C(2) : UINT32_C(4);
        identities[i].bound_operations |= operation_bit;
        for (unsigned j = 0; j < identities[i].argument_count && ok; ++j)
        {
            if (strcmp(identities[i].argument_operations[j], operation)) continue;
            char const* expected = !strcmp(identities[i].arguments[j], "$EXECUTABLE") ? executable : identities[i].arguments[j];
            int found = 0;
            for (unsigned k = 0; expected && k < expanded->count; ++k)
                if (!strcmp(expanded->values[k], expected)) { found = 1; break; }
            ok = found;
            if (found) identities[i].bound_arguments |= UINT32_C(1) << j;
        }
    }
    if (!ok) tp_error("identity manifest argument is not present in expanded %s argv", operation);
    return ok;
}

static uint32_t tp_workload_operation_mask(char const* operations)
{
    return (tp_workload_csv_contains(operations, "source-to-object") ? UINT32_C(1) : 0) |
           (tp_workload_csv_contains(operations, "source-to-linked-executable") ? UINT32_C(2) : 0) |
           (tp_workload_csv_contains(operations, "runtime") ? UINT32_C(4) : 0);
}

static void tp_workload_json_command_closures(FILE* file, TpWorkloadIdentity identities[6], char const* operation)
{
    fputs(",\"closures\":[", file);
    int previous = 0;
    for (unsigned i = 0; i < 6; ++i)
    {
        if (!tp_workload_csv_contains(identities[i].operation, operation)) continue;
        if (previous) fputc(',', file);
        fputs("{\"path\":", file); tp_json_string(file, identities[i].path);
        fputs(",\"sha256\":", file); tp_json_string(file, identities[i].sha256); fputc('}', file);
        previous = 1;
    }
    fputc(']', file);
}

static int tp_workload_command_record(FILE* file, char const* operation, char const* source, char const* cwd,
                                      TpWorkloadExpandedArguments const* arguments, TpWorkloadIdentity identities[6])
{
    fputs("{\"operation\":", file); tp_json_string(file, operation);
    fputs(",\"source\":", file);
    if (source) tp_json_string(file, source); else fputs("null", file);
    fputs(",\"cwd\":", file); tp_json_string(file, cwd);
    fputs(",\"argv\":[", file);
    for (unsigned i = 0; i < arguments->count; ++i)
    {
        if (i) fputc(',', file);
        tp_json_string(file, arguments->values[i]);
    }
    fputc(']', file); tp_workload_json_command_closures(file, identities, operation); fputs("}\n", file);
    return !ferror(file) && fflush(file) == 0;
}

static int tp_workload_process_pass(TpWorkloadExpandedArguments const* arguments, char const* cwd, char const* log, TpProcess* result)
{
    *result = tp_process(arguments->values, cwd, log, 120, -1, 0);
    return !result->launch_error && !result->timed_out && !result->signal_number && result->exit_code == 0;
}

static int tp_workload_result_record(FILE* file, char const* operation, char const* source, TpProcess const* result,
                                     TpWorkloadIdentity identities[6])
{
    fputs("{\"operation_result\":", file); tp_json_string(file, operation);
    fputs(",\"source\":", file);
    if (source) tp_json_string(file, source); else fputs("null", file);
    fprintf(file, ",\"exit_code\":%d,\"signal\":%d,\"timeout\":%d,\"launch_error\":%d",
            result->exit_code, result->signal_number, result->timed_out, result->launch_error);
    tp_workload_json_command_closures(file, identities, operation); fputs("}\n", file);
    return !ferror(file) && fflush(file) == 0;
}

static void tp_workload_json_identity(FILE* file, char const* key, char const* declared, TpWorkloadIdentity const* identity)
{
    fputs(",\"", file); fputs(key, file); fputs("\":{\"declared\":", file); tp_json_string(file, declared);
    fputs(",\"resolved\":", file); tp_json_string(file, identity->identity);
    fputs(",\"bound_operations\":", file); tp_json_string(file, identity->operation);
    fputs(",\"path\":", file); tp_json_string(file, identity->path);
    fputs(",\"sha256\":", file); tp_json_string(file, identity->sha256);
    fprintf(file, ",\"bytes\":%" PRIu64 ",\"files\":%u,\"trees\":%u,\"facts\":%u,\"operation_bound\":true,\"argv_bound\":",
            identity->bytes, identity->file_count, identity->tree_count, identity->fact_count);
    if (identity->argument_count) fputs("true", file); else fputs("null", file);
    fputs(",\"arguments\":[", file);
    for (unsigned i = 0; i < identity->argument_count; ++i)
    {
        if (i) fputc(',', file);
        fputs("{\"operation\":", file); tp_json_string(file, identity->argument_operations[i]);
        fputs(",\"value\":", file); tp_json_string(file, identity->arguments[i]); fputc('}', file);
    }
    fputs("]}", file);
}

static int tp_workload_admit(TpWorkloadAdmitOptions options)
{
    TpWorkloadDescriptor descriptor = {0};
    char descriptor_path[TP_PATH_CAP], root[TP_PATH_CAP], compiler[TP_PATH_CAP], evidence[TP_PATH_CAP], output[TP_PATH_CAP], cwd[TP_PATH_CAP];
    char descriptor_sha256[65], tree_sha256[65], compiler_sha256[65], evidence_sha256[65], commands_sha256[65], runtime_sha256[65];
    uint64_t descriptor_bytes = 0, compiler_bytes = 0, evidence_bytes = 0, commands_bytes = 0, runtime_bytes = 0, lines = 0;
    TpWorkloadIdentity identities[6];
    char const* identity_paths[6] = {options.dependency_manifest, options.resource_manifest, options.sysroot_manifest,
                                     options.sdk_manifest, options.environment_manifest, options.runtime_manifest};
    char const* identity_kinds[6] = {"dependency", "resource", "sysroot", "sdk", "environment", "runtime"};
    char const* declared_identities[6] = {descriptor.dependency_identity, descriptor.resource_identity, descriptor.sysroot_identity,
                                          descriptor.sdk_identity, descriptor.environment_identity, descriptor.runtime_identity};
    int ok = options.check.descriptor && options.check.source_root && options.check.compiler && options.check.evidence &&
             options.check.evidence_outcome && !strcmp(options.check.evidence_outcome, "pass") && options.output &&
             options.qualification_id && options.qualification_id[0] && strlen(options.qualification_id) < TP_WORKLOAD_TEXT_CAP &&
             tp_absolute(options.check.descriptor, descriptor_path) && tp_absolute(options.check.source_root, root) &&
             tp_absolute(options.check.compiler, compiler) && tp_absolute(options.check.evidence, evidence) &&
             tp_absolute(options.output, output) && tp_absolute(".", cwd) && tp_workload_regular_file(descriptor_path) &&
             tp_workload_directory(root) && tp_workload_regular_file(compiler) && tp_workload_regular_file(evidence) &&
             tp_workload_descriptor_parse(descriptor_path, &descriptor) && !strcmp(descriptor.schema, TP_WORKLOAD_DESCRIPTOR_SCHEMA) &&
             tp_workload_descriptor_inputs(root, &descriptor, tree_sha256) &&
             tp_hash_file(descriptor_path, descriptor_sha256, &descriptor_bytes, &lines) &&
             tp_hash_file(compiler, compiler_sha256, &compiler_bytes, &lines) &&
             tp_hash_file(evidence, evidence_sha256, &evidence_bytes, &lines) && evidence_bytes > 0 &&
             tp_workload_file_contains_line(evidence, descriptor.oracle_success);
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(identities) && ok; ++i)
        ok = tp_workload_identity(identity_paths[i], identity_kinds[i], identities + i);
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(declared_identities) && ok; ++i)
        ok = !strcmp(declared_identities[i], identities[i].identity);
    struct stat exists;
    if (ok)
    {
        ok = stat(output, &exists) != 0 && errno == ENOENT && tp_mkdirs(output);
        if (!ok) tp_error("admission output already exists or cannot be created: %s", output);
    }
    unsigned source_count = 0;
    for (unsigned i = 0; i < descriptor.input_count; ++i) source_count += tp_workload_source_input(descriptor.inputs + i);
    TpWorkloadArtifact* artifacts = ok ? calloc(source_count + 2, sizeof(*artifacts)) : NULL;
    ok = ok && artifacts != NULL;
    char commands_path[TP_PATH_CAP], runtime_path[TP_PATH_CAP];
    FILE* commands = ok && tp_path(commands_path, output, "commands.jsonl") ? fopen(commands_path, "wb") : NULL;
    ok = ok && commands != NULL;
    unsigned artifact_count = 0, source_index = 0;
    uint64_t object_source_bytes = 0, object_translated_bytes = 0, object_translated_lines = 0;
    TpProcess runtime_process = {0};
    for (unsigned i = 0; i < descriptor.input_count && ok; ++i)
    {
        TpWorkloadInput const* input = descriptor.inputs + i;
        if (!tp_workload_source_input(input)) continue;
        TpWorkloadArtifact* artifact = artifacts + artifact_count;
        char source[TP_PATH_CAP], metrics[TP_PATH_CAP], log[TP_PATH_CAP], leaf[128];
        snprintf(leaf, sizeof(leaf), "object-%03u.o", source_index);
        ok = tp_path(source, root, input->path) && tp_path(artifact->path, output, leaf);
        snprintf(leaf, sizeof(leaf), "object-%03u.metrics", source_index);
        ok = ok && tp_path(metrics, output, leaf) && tp_workload_text_copy(artifact->metrics_path, sizeof(artifact->metrics_path), metrics);
        snprintf(leaf, sizeof(leaf), "object-%03u.log", source_index);
        ok = ok && tp_path(log, output, leaf);
        TpWorkloadExpandedArguments expanded;
        TpProcess process;
        ok = ok && tp_workload_expand_arguments(&descriptor, compiler, root, descriptor.object_arguments,
                                                 descriptor.object_argument_count, source, artifact->path, metrics, NULL, &expanded) &&
             (remove(artifact->path) == 0 || errno == ENOENT) && (remove(metrics) == 0 || errno == ENOENT) &&
             tp_workload_bind_identities(identities, "source-to-object", &expanded, NULL) &&
             tp_workload_command_record(commands, "source-to-object", input->path, cwd, &expanded, identities);
        if (ok)
        {
            int passed = tp_workload_process_pass(&expanded, cwd, log, &process);
            int recorded = tp_workload_result_record(commands, "source-to-object", input->path, &process, identities);
            ok = passed && recorded;
        }
        TpRow metrics_row = {0};
        if (ok) ok = tp_workload_regular_file(artifact->path) &&
                     tp_hash_file(artifact->path, artifact->sha256, &artifact->bytes, &lines) && artifact->bytes > 0 &&
                     tp_workload_regular_file(metrics) && tp_read_metrics(metrics, &metrics_row) &&
                     tp_hash_file(metrics, artifact->metrics_sha256, &artifact->metrics_bytes, &lines) && artifact->metrics_bytes > 0;
        if (ok) ok = UINT64_MAX - object_translated_bytes >= metrics_row.source_bytes &&
                     UINT64_MAX - object_translated_lines >= metrics_row.source_lines;
        if (ok)
        {
            strcpy(artifact->role, "object");
            strcpy(artifact->source, input->path);
            artifact->translated_bytes = metrics_row.source_bytes;
            artifact->translated_lines = metrics_row.source_lines;
            object_source_bytes += input->bytes;
            object_translated_bytes += metrics_row.source_bytes;
            object_translated_lines += metrics_row.source_lines;
            ++artifact_count;
            ++source_index;
        }
        else tp_error("source-to-object admission operation failed for %s", input->path);
    }
    char executable[TP_PATH_CAP], link_metrics[TP_PATH_CAP], link_log[TP_PATH_CAP];
    if (ok)
    {
#ifdef _WIN32
        ok = tp_path(executable, output, "program.exe");
#else
        ok = tp_path(executable, output, "program");
#endif
        ok = ok && tp_path(link_metrics, output, "compile-link.metrics") && tp_path(link_log, output, "compile-link.log");
        TpWorkloadExpandedArguments expanded;
        TpProcess process;
        ok = ok && tp_workload_expand_arguments(&descriptor, compiler, root, descriptor.compile_link_arguments,
                                                 descriptor.compile_link_argument_count, NULL, executable, link_metrics, NULL, &expanded) &&
             (remove(executable) == 0 || errno == ENOENT) && (remove(link_metrics) == 0 || errno == ENOENT) &&
             tp_workload_bind_identities(identities, "source-to-linked-executable", &expanded, NULL) &&
             tp_workload_command_record(commands, "source-to-linked-executable", NULL, cwd, &expanded, identities);
        if (ok)
        {
            int passed = tp_workload_process_pass(&expanded, cwd, link_log, &process);
            int recorded = tp_workload_result_record(commands, "source-to-linked-executable", NULL, &process, identities);
            ok = passed && recorded;
        }
        TpWorkloadArtifact* artifact = artifacts + artifact_count;
        TpRow metrics_row = {0};
        if (ok) ok = tp_workload_text_copy(artifact->metrics_path, sizeof(artifact->metrics_path), link_metrics) &&
                     tp_workload_regular_file(executable) && tp_hash_file(executable, artifact->sha256, &artifact->bytes, &lines) && artifact->bytes > 0 &&
                     tp_workload_regular_file(link_metrics) && tp_read_metrics(link_metrics, &metrics_row) &&
                     tp_hash_file(link_metrics, artifact->metrics_sha256, &artifact->metrics_bytes, &lines) && artifact->metrics_bytes > 0;
        if (ok)
        {
            strcpy(artifact->role, "executable");
            strcpy(artifact->path, executable);
            artifact->translated_bytes = metrics_row.source_bytes;
            artifact->translated_lines = metrics_row.source_lines;
            ++artifact_count;
        }
        else tp_error("source-to-linked-executable admission operation failed");
    }
    if (ok)
    {
        TpWorkloadExpandedArguments expanded;
        ok = tp_path(runtime_path, output, "runtime.log") &&
             tp_workload_expand_arguments(&descriptor, compiler, root, descriptor.runtime_arguments,
                                           descriptor.runtime_argument_count, NULL, NULL, NULL, executable, &expanded) &&
             tp_workload_bind_identities(identities, "runtime", &expanded, executable) &&
             tp_workload_command_record(commands, "runtime", NULL, cwd, &expanded, identities);
        if (ok)
        {
            int passed = tp_workload_process_pass(&expanded, cwd, runtime_path, &runtime_process);
            int recorded = tp_workload_result_record(commands, "runtime", NULL, &runtime_process, identities);
            ok = passed && recorded;
        }
        ok = ok && tp_workload_regular_file(runtime_path) && tp_hash_file(runtime_path, runtime_sha256, &runtime_bytes, &lines) &&
             runtime_bytes > 0 && !strcmp(runtime_sha256, descriptor.runtime_transcript_sha256);
        if (ok)
        {
            TpWorkloadArtifact* artifact = artifacts + artifact_count;
            strcpy(artifact->role, "runtime-transcript");
            strcpy(artifact->path, runtime_path);
            strcpy(artifact->sha256, runtime_sha256);
            artifact->bytes = runtime_bytes;
            ++artifact_count;
        }
        if (!ok) tp_error("runtime admission operation failed or transcript changed");
    }
    if (commands && fclose(commands) != 0) ok = 0;
    if (ok) ok = tp_hash_file(commands_path, commands_sha256, &commands_bytes, &lines) && commands_bytes > 0;
    uint64_t link_source_bytes = 0;
    for (unsigned i = 0; i < descriptor.link_input_count; ++i)
        for (unsigned j = 0; j < descriptor.input_count; ++j)
            if (!strcmp(descriptor.link_inputs[i], descriptor.inputs[j].path)) link_source_bytes += descriptor.inputs[j].bytes;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(identities) && ok; ++i)
        ok = identities[i].bound_operations == tp_workload_operation_mask(identities[i].operation) &&
             identities[i].bound_arguments == (UINT32_C(1) << identities[i].argument_count) - 1;
    for (unsigned i = 0; i < artifact_count && ok; ++i)
    {
        char after_sha256[65];
        uint64_t after_bytes = 0;
        ok = tp_workload_regular_file(artifacts[i].path) &&
             tp_hash_file(artifacts[i].path, after_sha256, &after_bytes, &lines) &&
             after_bytes == artifacts[i].bytes && !strcmp(after_sha256, artifacts[i].sha256);
        if (ok && artifacts[i].metrics_path[0])
            ok = tp_workload_regular_file(artifacts[i].metrics_path) &&
                 tp_hash_file(artifacts[i].metrics_path, after_sha256, &after_bytes, &lines) &&
                 after_bytes == artifacts[i].metrics_bytes && !strcmp(after_sha256, artifacts[i].metrics_sha256);
    }
    if (ok)
    {
        TpWorkloadDescriptor after_descriptor = {0};
        char after_descriptor_sha256[65], after_tree_sha256[65], after_compiler_sha256[65], after_evidence_sha256[65];
        uint64_t after_bytes = 0;
        ok = tp_workload_regular_file(descriptor_path) && tp_workload_regular_file(compiler) && tp_workload_regular_file(evidence) &&
             tp_workload_descriptor_parse(descriptor_path, &after_descriptor) &&
             tp_workload_descriptor_inputs(root, &after_descriptor, after_tree_sha256) &&
             tp_hash_file(descriptor_path, after_descriptor_sha256, &after_bytes, &lines) && after_bytes == descriptor_bytes &&
             !strcmp(after_descriptor_sha256, descriptor_sha256) && !strcmp(after_tree_sha256, tree_sha256) &&
             tp_hash_file(compiler, after_compiler_sha256, &after_bytes, &lines) && after_bytes == compiler_bytes &&
             !strcmp(after_compiler_sha256, compiler_sha256) &&
             tp_hash_file(evidence, after_evidence_sha256, &after_bytes, &lines) && after_bytes == evidence_bytes &&
             !strcmp(after_evidence_sha256, evidence_sha256);
        for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(identities) && ok; ++i)
        {
            TpWorkloadIdentity after_identity;
            ok = tp_workload_regular_file(identities[i].path) && tp_workload_identity(identity_paths[i], identity_kinds[i], &after_identity) &&
                 after_identity.bytes == identities[i].bytes && !strcmp(after_identity.sha256, identities[i].sha256);
        }
        if (!ok) tp_error("admission inputs or closure identities drifted during execution");
    }
    if (ok)
    {
        fputs("{\"schema\":\"buster-throughput-workload-admission-v1\",\"scope\":\"hosted-functional-admission\",", stdout);
        fputs("\"admitted\":true,\"fresh_admission_required\":false,\"qualification_id\":", stdout);
        tp_json_string(stdout, options.qualification_id);
        fputs(",\"name\":", stdout); tp_json_string(stdout, descriptor.name);
        fputs(",\"family\":", stdout); tp_json_string(stdout, descriptor.family);
        fputs(",\"historical_outcome\":", stdout); tp_json_string(stdout, descriptor.historical_outcome);
        fputs(",\"historical_evidence\":", stdout); tp_json_string(stdout, descriptor.historical_evidence);
        fputs(",\"source_identity\":{\"declared\":", stdout); tp_json_string(stdout, descriptor.source_identity);
        fputs(",\"tree_sha256\":", stdout); tp_json_string(stdout, tree_sha256); fputc('}', stdout);
        fputs(",\"generated_identity\":", stdout); tp_json_string(stdout, descriptor.generated_identity);
        tp_workload_json_identity(stdout, "dependency_identity", descriptor.dependency_identity, identities + 0);
        tp_workload_json_identity(stdout, "resource_identity", descriptor.resource_identity, identities + 1);
        tp_workload_json_identity(stdout, "sysroot_identity", descriptor.sysroot_identity, identities + 2);
        tp_workload_json_identity(stdout, "sdk_identity", descriptor.sdk_identity, identities + 3);
        tp_workload_json_identity(stdout, "environment_identity", descriptor.environment_identity, identities + 4);
        tp_workload_json_identity(stdout, "runtime_identity", descriptor.runtime_identity, identities + 5);
        fputs(",\"compiler\":{\"path\":", stdout); tp_json_string(stdout, compiler);
        fputs(",\"sha256\":", stdout); tp_json_string(stdout, compiler_sha256);
        fprintf(stdout, ",\"bytes\":%" PRIu64 "}", compiler_bytes);
        fputs(",\"descriptor\":{\"path\":", stdout); tp_json_string(stdout, descriptor_path);
        fputs(",\"sha256\":", stdout); tp_json_string(stdout, descriptor_sha256);
        fprintf(stdout, ",\"bytes\":%" PRIu64 "}", descriptor_bytes);
        fputs(",\"oracle_evidence\":{\"path\":", stdout); tp_json_string(stdout, evidence);
        fputs(",\"sha256\":", stdout); tp_json_string(stdout, evidence_sha256);
        fprintf(stdout, ",\"bytes\":%" PRIu64 ",\"outcome\":\"pass\"}", evidence_bytes);
        fputs(",\"combination\":{\"target\":", stdout); tp_json_string(stdout, descriptor.target);
        fputs(",\"abi\":", stdout); tp_json_string(stdout, descriptor.abi);
        fputs(",\"cpu\":", stdout); tp_json_string(stdout, descriptor.cpu);
        fputs(",\"cpu_features\":", stdout); tp_json_string(stdout, descriptor.cpu_features);
        fputs(",\"frontend\":", stdout); tp_json_string(stdout, descriptor.admission_frontend);
        fputs(",\"pic\":", stdout); tp_json_string(stdout, descriptor.admission_pic);
        fputs(",\"allocator\":", stdout); tp_json_string(stdout, descriptor.admission_allocator); fputc('}', stdout);
        fputs(",\"performed_cells\":[", stdout);
        char const* cell_operations[] = {"source-to-object", "source-to-linked-executable", "runtime"};
        for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(cell_operations); ++i)
        {
            if (i) fputc(',', stdout);
            fputs("{\"operation\":", stdout); tp_json_string(stdout, cell_operations[i]);
            fputs(",\"target\":", stdout); tp_json_string(stdout, descriptor.target);
            fputs(",\"cpu\":", stdout); tp_json_string(stdout, descriptor.cpu);
            fputs(",\"frontend\":", stdout); tp_json_string(stdout, descriptor.admission_frontend);
            fputs(",\"pic\":", stdout); tp_json_string(stdout, descriptor.admission_pic);
            fputs(",\"allocator\":", stdout); tp_json_string(stdout, descriptor.admission_allocator); fputc('}', stdout);
        }
        fputc(']', stdout);
        fputs(",\"requested_configuration\":{\"c_lowerings\":", stdout); tp_json_string(stdout, descriptor.c_lowerings);
        fputs(",\"pic_modes\":", stdout); tp_json_string(stdout, descriptor.pic_modes);
        fputs(",\"allocator_modes\":", stdout); tp_json_string(stdout, descriptor.allocator_modes);
        fputs(",\"operations\":", stdout); tp_json_string(stdout, descriptor.operations);
        fputs(",\"artifacts\":", stdout); tp_json_string(stdout, descriptor.artifacts); fputc('}', stdout);
        fprintf(stdout, ",\"requested_work\":{\"object\":{\"source_count\":%u,\"raw_source_bytes\":%" PRIu64
                        "},\"compile_link\":{\"source_count\":%u,\"raw_source_bytes\":%" PRIu64
                        "},\"runtime\":{\"argv_count\":%u,\"transcript_sha256\":", source_count, object_source_bytes,
                descriptor.link_input_count, link_source_bytes, descriptor.runtime_argument_count);
        tp_json_string(stdout, descriptor.runtime_transcript_sha256);
        fputs("}}", stdout);
        TpWorkloadArtifact const* executable_artifact = artifacts + source_count;
        fprintf(stdout, ",\"performed_work\":{\"object\":{\"command_count\":%u,\"artifact_count\":%u,\"translated_bytes\":%" PRIu64
                        ",\"translated_lines\":%" PRIu64 "},\"compile_link\":{\"command_count\":1,\"artifact_count\":1,\"translated_bytes\":%" PRIu64
                        ",\"translated_lines\":%" PRIu64 "},\"runtime\":{\"executed\":true,\"transcript_sha256\":",
                source_count, source_count, object_translated_bytes, object_translated_lines,
                executable_artifact->translated_bytes, executable_artifact->translated_lines);
        tp_json_string(stdout, runtime_sha256);
        fprintf(stdout, ",\"bytes\":%" PRIu64 ",\"exit_code\":%d,\"signal\":%d,\"timeout\":%d,\"launch_error\":%d}}",
                runtime_bytes, runtime_process.exit_code, runtime_process.signal_number, runtime_process.timed_out, runtime_process.launch_error);
        fputs(",\"argv_identity\":{\"path\":", stdout); tp_json_string(stdout, commands_path);
        fputs(",\"sha256\":", stdout); tp_json_string(stdout, commands_sha256);
        fprintf(stdout, ",\"bytes\":%" PRIu64 "}", commands_bytes);
        fputs(",\"artifacts\":[", stdout);
        for (unsigned i = 0; i < artifact_count; ++i)
        {
            if (i) fputc(',', stdout);
            fputs("{\"role\":", stdout); tp_json_string(stdout, artifacts[i].role);
            fputs(",\"source\":", stdout);
            if (artifacts[i].source[0]) tp_json_string(stdout, artifacts[i].source); else fputs("null", stdout);
            fputs(",\"path\":", stdout); tp_json_string(stdout, artifacts[i].path);
            fputs(",\"sha256\":", stdout); tp_json_string(stdout, artifacts[i].sha256);
            fprintf(stdout, ",\"bytes\":%" PRIu64, artifacts[i].bytes);
            if (artifacts[i].metrics_path[0])
            {
                fputs(",\"metrics\":{\"path\":", stdout); tp_json_string(stdout, artifacts[i].metrics_path);
                fputs(",\"sha256\":", stdout); tp_json_string(stdout, artifacts[i].metrics_sha256);
                fprintf(stdout, ",\"bytes\":%" PRIu64 ",\"translated_bytes\":%" PRIu64 ",\"translated_lines\":%" PRIu64 "}",
                        artifacts[i].metrics_bytes, artifacts[i].translated_bytes, artifacts[i].translated_lines);
            }
            fputc('}', stdout);
        }
        fputs("]}\n", stdout);
        ok = !ferror(stdout) && fflush(stdout) == 0;
    }
    if (!ok) tp_error("workload functional admission failed; no admission receipt emitted");
    free(artifacts);
    return ok;
}

#endif
