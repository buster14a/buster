// Production Clang PGO/LTO workflow, included by build.c.
//
// The workflow deliberately stays outside ordinary generate/build defaults:
// it creates fresh, named trees, trains one instrumented Release compiler on
// the deterministic native throughput corpus, seals the merged profile with a
// source/tool/workload manifest, and only then admits PGO-use builds.
#define BUSTER_PRODUCTION_PROFILE_TIMEOUT_SECONDS_DEFAULT 3600ull
#define BUSTER_PRODUCTION_PROFILE_TRAINING_PAIRS 1ull
#define BUSTER_PRODUCTION_PROFILE_TRAINING_WARMUPS 1ull
#define BUSTER_PRODUCTION_PROFILE_MANIFEST_VERSION "BUSTER_PGO_PROFILE_V1"
#define BUSTER_PRODUCTION_PROFILE_CONTRACT_VERSION "BUSTER_PGO_TRAINING_V1"

typedef struct ProductionProfileOptions ProductionProfileOptions;
struct ProductionProfileOptions
{
    String8 output;
    String8 clang;
    String8 llvm_profdata;
    String8 llvm_readobj;
    String8 benchmark_profile;
    u64 jobs;
    u64 pairs;
    u64 warmups;
    u64 timeout_seconds;
    bool clean;
    bool benchmark;
};

typedef struct ProductionProfileCommandResult ProductionProfileCommandResult;
struct ProductionProfileCommandResult
{
    ProcessWaitResult wait;
    String8 output;
    String8 error;
    bool success;
};

typedef struct ProductionProfileContext ProductionProfileContext;
struct ProductionProfileContext
{
    Arena* arena;
    ProductionProfileOptions options;
    String8 driver;
    String8 repository;
    String8 output;
    String8 evidence;
    String8 clang;
    String8 llvm_profdata;
    String8 llvm_readobj;
    String8 revision;
    String8 tree;
    String8 fingerprint;
    String8 profile;
    String8 manifest;
};

typedef struct ProductionProfileVariant ProductionProfileVariant;
struct ProductionProfileVariant
{
    String8 name;
    bool debug_info;
    bool lto;
    bool generate;
    bool use;
};

BUSTER_GLOBAL_LOCAL bool production_profile_u64(String8 text, u64* value)
{
    bool result = text.length != 0;
    u64 parsed = 0;
    for (u64 index = 0; result && index < text.length; index += 1)
    {
        u8 digit = (u8)text.pointer[index];
        result = digit >= '0' && digit <= '9';
        if (result)
        {
            u64 value_digit = (u64)(digit - '0');
            result = parsed <= (UINT64_MAX - value_digit) / 10;
            if (result)
            {
                parsed = parsed * 10 + value_digit;
            }
        }
    }
    if (result)
    {
        *value = parsed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_trim(String8 text)
{
    u64 first = 0;
    u64 one_past_last = text.length;
    while (first < one_past_last)
    {
        char8 byte = text.pointer[first];
        if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n')
        {
            break;
        }
        first += 1;
    }
    while (one_past_last > first)
    {
        char8 byte = text.pointer[one_past_last - 1];
        if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n')
        {
            break;
        }
        one_past_last -= 1;
    }
    return (String8){.pointer = text.pointer + first, .length = one_past_last - first};
}

BUSTER_GLOBAL_LOCAL bool production_profile_contains(String8 text, String8 needle)
{
    return needle.length != 0 && string_first_sequence(text, needle) != BUSTER_STRING_NO_MATCH;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_sha256_bytes(Arena* arena, void const* bytes, u64 size)
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, size);
    char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
    sha256_finish_hex(&hash, digits);
    return (String8){.pointer = digits, .length = SHA256_HEX_CAPACITY - 1};
}

BUSTER_GLOBAL_LOCAL String8 production_profile_sha256_text(Arena* arena, String8 text)
{
    return production_profile_sha256_bytes(arena, text.pointer, text.length);
}

BUSTER_GLOBAL_LOCAL String8 production_profile_sha256_file(Arena* arena, String8 path)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){0});
    String8 result = {0};
    if (bytes.pointer && bytes.length)
    {
        result = production_profile_sha256_bytes(arena, bytes.pointer, bytes.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool production_profile_write(String8 path, String8 text)
{
    return file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text));
}

BUSTER_GLOBAL_LOCAL String8 production_profile_argv_text(Arena* arena, SliceString8 arguments)
{
    String8List lines = {0};
    for (u64 index = 0; index < arguments.length; index += 1)
    {
        string8_list_push(arena, &lines, string_format(arena, S8("{u64}:{S8}\n"), arguments.pointer[index].length, arguments.pointer[index]));
    }
    return string_join_arena(arena, string8_list_to_slice(arena, lines), false);
}

BUSTER_GLOBAL_LOCAL bool production_profile_environment_key_equal(String8 left, String8 right)
{
    bool result = left.length == right.length;
    for (u64 index = 0; result && index < left.length; index += 1)
    {
        char8 left_byte = left.pointer[index];
        char8 right_byte = right.pointer[index];
#if BUSTER_WINDOWS
        if (left_byte >= 'A' && left_byte <= 'Z')
        {
            left_byte = (char8)(left_byte + ('a' - 'A'));
        }
        if (right_byte >= 'A' && right_byte <= 'Z')
        {
            right_byte = (char8)(right_byte + ('a' - 'A'));
        }
#endif
        result = left_byte == right_byte;
    }
    return result;
}

// os_process_spawn intentionally treats an explicit key/value list as the
// complete child environment. Materialize the captured parent environment,
// remove entries replaced by the caller, and append the caller's values.
BUSTER_GLOBAL_LOCAL bool production_profile_environment_merge(
    Arena* arena,
    SliceString8 inherited_keys, SliceString8 inherited_values,
    SliceString8 override_keys, SliceString8 override_values,
    SliceString8* result_keys, SliceString8* result_values)
{
    bool result = inherited_keys.length == inherited_values.length &&
                  override_keys.length == override_values.length &&
                  (!inherited_keys.length || (inherited_keys.pointer && inherited_values.pointer)) &&
                  (!override_keys.length || (override_keys.pointer && override_values.pointer)) &&
                  inherited_keys.length <= UINT64_MAX - override_keys.length;
    if (!result)
    {
        return false;
    }

    u64 capacity = inherited_keys.length + override_keys.length;
    String8* keys = capacity ? arena_allocate(arena, String8, capacity) : 0;
    String8* values = capacity ? arena_allocate(arena, String8, capacity) : 0;
    u64 count = 0;

    for (u64 inherited_index = 0; inherited_index < inherited_keys.length; inherited_index += 1)
    {
        bool replaced = false;
        for (u64 override_index = 0; override_index < override_keys.length; override_index += 1)
        {
            if (production_profile_environment_key_equal(
                    inherited_keys.pointer[inherited_index], override_keys.pointer[override_index]))
            {
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            keys[count] = inherited_keys.pointer[inherited_index];
            values[count] = inherited_values.pointer[inherited_index];
            count += 1;
        }
    }

    for (u64 override_index = 0; override_index < override_keys.length; override_index += 1)
    {
        keys[count] = override_keys.pointer[override_index];
        values[count] = override_values.pointer[override_index];
        count += 1;
    }

    *result_keys = (SliceString8){.pointer = keys, .length = count};
    *result_values = (SliceString8){.pointer = values, .length = count};
    return true;
}

BUSTER_GLOBAL_LOCAL ProductionProfileCommandResult production_profile_command(
    Arena* arena, SliceString8 arguments, SliceString8 environment_keys, SliceString8 environment_values,
    String8 evidence, String8 label, u64 timeout_seconds, bool capture)
{
    ProductionProfileCommandResult result = {0};
    if (evidence.length)
    {
        String8 argv_path = path_join(arena, evidence, string_format(arena, S8("{S8}.argv.txt"), label));
        if (!production_profile_write(argv_path, production_profile_argv_text(arena, arguments)))
        {
            fprintf(stderr, "error: could not write production-profile command evidence\n");
            return result;
        }
    }

    bool use_process_environment = !environment_keys.length && !environment_values.length;
    SliceString8 child_environment_keys = environment_keys;
    SliceString8 child_environment_values = environment_values;
    if (!use_process_environment &&
        !production_profile_environment_merge(
            arena,
            program_state->input.environment_keys, program_state->input.environment_values,
            environment_keys, environment_values,
            &child_environment_keys, &child_environment_values))
    {
        fprintf(stderr, "error: invalid production-profile environment override\n");
        return result;
    }

    command_print(arguments);
    u64 capture_mask = capture ? (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR) : 0;
    ProcessSpawnResult spawn = os_process_spawn(arguments, child_environment_keys, child_environment_values,
        (ProcessSpawnOptions){.capture = capture_mask, .use_process_environment = use_process_environment,
                              .search_path = 1, .new_process_group = 1});
    if (spawn.handle)
    {
        result.wait = os_process_wait_deadline(arena, spawn, timeout_seconds ? timeout_seconds * 1000000ull : 0);
    }
    else
    {
        result.wait.result = PROCESS_RESULT_NOT_EXISTENT;
    }

    result.output = (String8){
        .pointer = (char8*)result.wait.streams[STANDARD_STREAM_OUTPUT].pointer,
        .length = result.wait.streams[STANDARD_STREAM_OUTPUT].length,
    };
    result.error = (String8){
        .pointer = (char8*)result.wait.streams[STANDARD_STREAM_ERROR].pointer,
        .length = result.wait.streams[STANDARD_STREAM_ERROR].length,
    };
    result.success = result.wait.result == PROCESS_RESULT_SUCCESS && result.wait.platform_status == 0 && !result.wait.timed_out;

    if (evidence.length)
    {
        String8 status_path = path_join(arena, evidence, string_format(arena, S8("{S8}.status.txt"), label));
        String8 status = string_format(arena, S8("result={u32}\nplatform_status={u32}\ntimed_out={u32}\nsuccess={u32}\n"
            "spawn_failure={u32}\nspawn_error={u32}\n"),
            (u32)result.wait.result, result.wait.platform_status, (u32)result.wait.timed_out, (u32)result.success,
            (u32)spawn.failure, spawn.error.v);
        bool wrote = production_profile_write(status_path, status);
        if (capture)
        {
            wrote &= production_profile_write(path_join(arena, evidence, string_format(arena, S8("{S8}.stdout.log"), label)), result.output);
            wrote &= production_profile_write(path_join(arena, evidence, string_format(arena, S8("{S8}.stderr.log"), label)), result.error);
        }
        if (!wrote)
        {
            fprintf(stderr, "error: could not write production-profile process evidence\n");
            result.success = false;
        }
    }

    if (!result.success && capture)
    {
        if (result.output.length)
        {
            os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(result.output));
        }
        if (result.error.length)
        {
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(result.error));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProductionProfileCommandResult production_profile_capture(Arena* arena, SliceString8 arguments)
{
    return production_profile_command(arena, arguments, (SliceString8){0}, (SliceString8){0}, (String8){0}, (String8){0}, 120, true);
}

BUSTER_GLOBAL_LOCAL bool production_profile_path_is_child(String8 parent, String8 child)
{
    bool result = parent.length && child.length > parent.length && !memcmp(parent.pointer, child.pointer, (size_t)parent.length);
    if (result)
    {
        char8 separator = child.pointer[parent.length];
        result = separator == '/' || separator == '\\';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool production_profile_path_components_safe(String8 path)
{
    u64 first = 0;
    for (u64 one_past_last = 0; one_past_last <= path.length; one_past_last += 1)
    {
        bool separator = one_past_last == path.length || path.pointer[one_past_last] == '/' || path.pointer[one_past_last] == '\\';
        if (!separator)
        {
            continue;
        }
        String8 component = {.pointer = path.pointer + first, .length = one_past_last - first};
        if (string_equal(component, S8(".")) || string_equal(component, S8("..")))
        {
            return false;
        }
        first = one_past_last + 1;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_resolve_tool(Arena* arena, String8 requested, String8 description)
{
    String8 resolved = executable_resolve_in_path(arena, requested);
    if (!resolved.length)
    {
        fprintf(stderr, "error: could not resolve %.*s executable '%.*s'\n",
            string8_printf_length(description, UINT64_MAX), description.pointer,
            string8_printf_length(requested, UINT64_MAX), requested.pointer);
    }
    return resolved;
}

BUSTER_GLOBAL_LOCAL bool production_profile_debug_sections(String8 sections)
{
    String8 forbidden[] = {
        S8(".debug_"),
        S8(".zdebug_"),
        S8("__debug_"),
        S8("__DWARF"),
        S8(".gnu_debuglink"),
        S8(".gnu_debugaltlink"),
    };
    bool result = false;
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(forbidden); index += 1)
    {
        if (production_profile_contains(sections, forbidden[index]))
        {
            result = true;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void production_profile_usage(void)
{
    string_print(S8(
        "usage: production_profile [options]\n"
        "  --output PATH                 fresh evidence/build root (default build/production-profile)\n"
        "  --clang PATH                  Clang executable (default clang)\n"
        "  --llvm-profdata PATH          profile merger (default llvm-profdata)\n"
        "  --llvm-readobj PATH           section inspector (default llvm-readobj)\n"
        "  --benchmark-profile PROFILE   smoke, ci or full (default smoke)\n"
        "  --jobs N                      Ninja jobs per compiler build (default 2)\n"
        "  --pairs N                     benchmark pairs per comparison (default 8)\n"
        "  --warmups N                   benchmark warmups (default 1)\n"
        "  --timeout N                   child deadline in seconds (default 3600)\n"
        "  --no-benchmark                build/train/validate without the comparison matrix\n"
        "  --clean                       replace an existing output inside this checkout\n"));
}

BUSTER_GLOBAL_LOCAL bool production_profile_options(Arena* arena, SliceString8 arguments, ProductionProfileOptions* options)
{
    *options = (ProductionProfileOptions){
        .output = S8("build/production-profile"),
        .clang = S8("clang"),
        .llvm_profdata = S8("llvm-profdata"),
        .llvm_readobj = S8("llvm-readobj"),
        .benchmark_profile = S8("smoke"),
        .jobs = 2,
        .pairs = 8,
        .warmups = 1,
        .timeout_seconds = BUSTER_PRODUCTION_PROFILE_TIMEOUT_SECONDS_DEFAULT,
        .benchmark = true,
    };

    bool result = true;
    for (u64 index = 0; result && index < arguments.length; index += 1)
    {
        String8 argument = arguments.pointer[index];
        String8* string_value = 0;
        u64* integer_value = 0;
        if (string_equal(argument, S8("--output")))
        {
            string_value = &options->output;
        }
        else if (string_equal(argument, S8("--clang")))
        {
            string_value = &options->clang;
        }
        else if (string_equal(argument, S8("--llvm-profdata")))
        {
            string_value = &options->llvm_profdata;
        }
        else if (string_equal(argument, S8("--llvm-readobj")))
        {
            string_value = &options->llvm_readobj;
        }
        else if (string_equal(argument, S8("--benchmark-profile")))
        {
            string_value = &options->benchmark_profile;
        }
        else if (string_equal(argument, S8("--jobs")))
        {
            integer_value = &options->jobs;
        }
        else if (string_equal(argument, S8("--pairs")))
        {
            integer_value = &options->pairs;
        }
        else if (string_equal(argument, S8("--warmups")))
        {
            integer_value = &options->warmups;
        }
        else if (string_equal(argument, S8("--timeout")))
        {
            integer_value = &options->timeout_seconds;
        }
        else if (string_equal(argument, S8("--clean")))
        {
            options->clean = true;
        }
        else if (string_equal(argument, S8("--no-benchmark")))
        {
            options->benchmark = false;
        }
        else if (string_equal(argument, S8("--help")) || string_equal(argument, S8("-h")))
        {
            production_profile_usage();
            return false;
        }
        else
        {
            fprintf(stderr, "error: unknown production_profile argument '%.*s'\n",
                string8_printf_length(argument, UINT64_MAX), argument.pointer);
            result = false;
        }

        if (result && (string_value || integer_value))
        {
            index += 1;
            if (index == arguments.length)
            {
                fprintf(stderr, "error: option requires a value\n");
                result = false;
            }
            else if (string_value)
            {
                *string_value = arguments.pointer[index];
                result = string_value->length != 0;
            }
            else
            {
                result = production_profile_u64(arguments.pointer[index], integer_value);
                if (!result)
                {
                    fprintf(stderr, "error: expected an unsigned integer\n");
                }
            }
        }
    }

    if (result)
    {
        bool profile_valid = string_equal(options->benchmark_profile, S8("smoke")) ||
                             string_equal(options->benchmark_profile, S8("ci")) ||
                             string_equal(options->benchmark_profile, S8("full"));
        result = options->jobs && options->pairs && options->timeout_seconds && profile_valid;
        if (!result)
        {
            fprintf(stderr, "error: jobs, pairs and timeout must be nonzero and benchmark profile must be smoke, ci or full\n");
        }
    }

    BUSTER_UNUSED(arena);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_binary(Arena* arena, String8 build_directory)
{
#if BUSTER_WINDOWS
    return path_join(arena, build_directory, S8("Release/ide.exe"));
#else
    return path_join(arena, build_directory, S8("Release/ide"));
#endif
}

BUSTER_GLOBAL_LOCAL bool production_profile_build_variant(ProductionProfileContext* context, ProductionProfileVariant variant)
{
    Arena* arena = context->arena;
    String8 directory = path_join(arena, context->output, variant.name);
    String8 raw = path_join(arena, context->output, S8("profile/raw"));
    String8 debug = string_format(arena, S8("-DBUSTER_DEBUG_INFO={S8}"), variant.debug_info ? S8("ON") : S8("OFF"));
    String8 production = string_format(arena, S8("-DBUSTER_PRODUCTION_PROFILE={S8}"), variant.debug_info ? S8("OFF") : S8("ON"));
    String8 compiler = string_format(arena, S8("-DCMAKE_C_COMPILER:FILEPATH={S8}"), context->clang);
    String8 pgo_generate = string_format(arena, S8("-DBUSTER_PGO_GENERATE:PATH={S8}"), variant.generate ? raw : S8(""));
    String8 pgo_use = string_format(arena, S8("-DBUSTER_PGO_USE:FILEPATH={S8}"), variant.use ? context->profile : S8(""));
    String8 pgo_manifest = string_format(arena, S8("-DBUSTER_PGO_MANIFEST:FILEPATH={S8}"), variant.use ? context->manifest : S8(""));
    String8 pgo_fingerprint = string_format(arena, S8("-DBUSTER_PGO_FINGERPRINT:STRING={S8}"), variant.use ? context->fingerprint : S8(""));

    String8 generate[40];
    u64 count = 0;
    generate[count++] = context->driver;
    generate[count++] = S8("generate");
    generate[count++] = S8("--build-directory");
    generate[count++] = directory;
    generate[count++] = S8("--cc");
    generate[count++] = S8("clang");
    generate[count++] = S8("--linker");
    generate[count++] = S8("LLD");
    generate[count++] = S8("--no-ci");
    generate[count++] = S8("--include-tests");
    generate[count++] = S8("--no-sanitize");
    generate[count++] = S8("--no-fuzz");
    generate[count++] = S8("--no-time-trace");
    generate[count++] = S8("--no-instrument");
    generate[count++] = variant.lto ? S8("--lto") : S8("--no-lto");
    generate[count++] = S8("--");
    generate[count++] = compiler;
    generate[count++] = debug;
    generate[count++] = S8("-DBUSTER_FRAME_POINTERS=ON");
    generate[count++] = production;
    generate[count++] = pgo_generate;
    generate[count++] = pgo_use;
    generate[count++] = pgo_manifest;
    generate[count++] = pgo_fingerprint;

    String8 label = string_format(arena, S8("generate-{S8}"), variant.name);
    ProductionProfileCommandResult generated = production_profile_command(
        arena, (SliceString8){.pointer = generate, .length = count}, (SliceString8){0}, (SliceString8){0},
        context->evidence, label, context->options.timeout_seconds, false);
    if (!generated.success)
    {
        return false;
    }

    String8 jobs = string_format(arena, S8("-j{u64}"), context->options.jobs);
    String8 build[] = {
        context->driver, S8("build"), S8("--build-directory"), directory, S8("--config"), S8("Release"),
        S8("-t"), S8("ide"), S8("--"), jobs,
    };
    label = string_format(arena, S8("build-{S8}"), variant.name);
    ProductionProfileCommandResult built = production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(build), (SliceString8){0}, (SliceString8){0},
        context->evidence, label, context->options.timeout_seconds, false);
    return built.success && path_exists(arena, production_profile_binary(arena, directory));
}

BUSTER_GLOBAL_LOCAL bool production_profile_train(ProductionProfileContext* context, String8 instrumented)
{
    Arena* arena = context->arena;
    String8 raw = path_join(arena, context->output, S8("profile/raw"));
    String8 training = path_join(arena, context->output, S8("training"));
    make_directory_recursive(arena, raw);
    String8 profile_pattern = path_join(arena, raw, S8("%4m.profraw"));
    String8 timeout = string_format(arena, S8("{u64}"), context->options.timeout_seconds);
    String8 revision_id = string_format(arena, S8("{S8}:training"), context->revision);
    String8 command[] = {
        context->driver, S8("bench_throughput"), S8("run"),
        S8("--baseline"), instrumented, S8("--candidate"), instrumented,
        S8("--output"), training, S8("--profile"), S8("ci"), S8("--mode"), S8("all"),
        S8("--pairs"), S8("1"), S8("--warmups"), S8("1"), S8("--timeout"), timeout,
        S8("--cpu"), S8("auto"), S8("--no-guard"), S8("--require-identical-output"),
        S8("--baseline-id"), revision_id, S8("--candidate-id"), revision_id,
    };
    String8 keys[] = {S8("LLVM_PROFILE_FILE")};
    String8 values[] = {profile_pattern};
    ProductionProfileCommandResult trained = production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(keys), (SliceString8)BUSTER_ARRAY_TO_SLICE(values),
        context->evidence, S8("train"), context->options.timeout_seconds, false);
    return trained.success;
}

BUSTER_GLOBAL_LOCAL int production_profile_path_compare(const void* left_pointer, const void* right_pointer)
{
    const String8* left = left_pointer;
    const String8* right = right_pointer;
    u64 count = BUSTER_MIN(left->length, right->length);
    int result = count ? memcmp(left->pointer, right->pointer, (size_t)count) : 0;
    return result ? result : (left->length > right->length) - (left->length < right->length);
}

BUSTER_GLOBAL_LOCAL bool production_profile_merge(ProductionProfileContext* context)
{
#if BUSTER_LINUX || BUSTER_APPLE
    Arena* arena = context->arena;
    String8 raw = path_join(arena, context->output, S8("profile/raw"));
    String8 raw_z = string_duplicate_arena(arena, raw, true);
    DIR* directory = opendir((const char*)raw_z.pointer);
    if (!directory)
    {
        fprintf(stderr, "error: could not open the raw production-profile directory\n");
        return false;
    }

    u64 file_count = 0;
    errno = 0;
    for (struct dirent* entry = readdir(directory); entry; entry = readdir(directory))
    {
        String8 name = string_from_pointer((char8*)entry->d_name);
        file_count += string_ends_with_sequence(name, S8(".profraw"));
    }
    bool valid = errno == 0 && file_count != 0;
    rewinddir(directory);

    String8* files = valid ? arena_allocate(arena, String8, file_count) : 0;
    u64 file_index = 0;
    errno = 0;
    for (struct dirent* entry = valid ? readdir(directory) : 0; entry; entry = readdir(directory))
    {
        String8 name = string_from_pointer((char8*)entry->d_name);
        if (string_ends_with_sequence(name, S8(".profraw")))
        {
            if (file_index == file_count)
            {
                valid = false;
                break;
            }
            files[file_index++] = path_join(arena, raw, name);
        }
    }
    valid &= errno == 0 && file_index == file_count;
    closedir(directory);
    if (!valid)
    {
        fprintf(stderr, "error: could not enumerate raw production profiles\n");
        return false;
    }

    qsort(files, (size_t)file_count, sizeof(files[0]), production_profile_path_compare);
    String8 output_argument = string_format(arena, S8("-output={S8}"), context->profile);
    String8* command = arena_allocate(arena, String8, file_count + 3);
    command[0] = context->llvm_profdata;
    command[1] = S8("merge");
    command[2] = output_argument;
    for (u64 index = 0; index < file_count; index += 1)
    {
        command[index + 3] = files[index];
    }
    ProductionProfileCommandResult merged = production_profile_command(
        arena, (SliceString8){.pointer = command, .length = file_count + 3}, (SliceString8){0}, (SliceString8){0},
        context->evidence, S8("merge-profile"), context->options.timeout_seconds, true);
    return merged.success && path_exists(arena, context->profile);
#else
    BUSTER_UNUSED(context);
    return false;
#endif
}

BUSTER_GLOBAL_LOCAL bool production_profile_validate_binary(ProductionProfileContext* context, String8 binary)
{
    Arena* arena = context->arena;
    String8 command[] = {context->llvm_readobj, S8("--sections"), binary};
    ProductionProfileCommandResult inspected = production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
        context->evidence, S8("inspect-production-sections"), 300, true);
    if (!inspected.success)
    {
        return false;
    }
    if (production_profile_debug_sections(inspected.output))
    {
        fprintf(stderr, "error: production compiler contains a debug section\n");
        return false;
    }

    String8 final_directory = path_join(arena, context->output, S8("pgo-lto"));
    String8 jobs = string_format(arena, S8("-j{u64}"), context->options.jobs);
    String8 command_test[] = {
        context->driver, S8("build"), S8("--build-directory"), final_directory, S8("--config"), S8("Release"),
        S8("-t"), S8("test_all"), S8("--"), jobs,
    };
    ProductionProfileCommandResult tested = production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_test), (SliceString8){0}, (SliceString8){0},
        context->evidence, S8("test-production"), context->options.timeout_seconds, false);
    return tested.success;
}

BUSTER_GLOBAL_LOCAL bool production_profile_benchmark_one(
    ProductionProfileContext* context, String8 release, String8 candidate, String8 name)
{
    Arena* arena = context->arena;
    String8 directory = path_join(arena, path_join(arena, context->output, S8("benchmarks")), name);
    String8 pairs = string_format(arena, S8("{u64}"), context->options.pairs);
    String8 warmups = string_format(arena, S8("{u64}"), context->options.warmups);
    String8 timeout = string_format(arena, S8("{u64}"), context->options.timeout_seconds);
    String8 baseline_id = string_format(arena, S8("{S8}:release"), context->revision);
    String8 candidate_id = string_format(arena, S8("{S8}:{S8}"), context->revision, name);
    String8 run[] = {
        context->driver, S8("bench_throughput"), S8("run"),
        S8("--baseline"), release, S8("--candidate"), candidate, S8("--output"), directory,
        S8("--profile"), context->options.benchmark_profile, S8("--mode"), S8("all"),
        S8("--pairs"), pairs, S8("--warmups"), warmups, S8("--timeout"), timeout,
        S8("--cpu"), S8("auto"), S8("--no-guard"), S8("--require-identical-output"),
        S8("--baseline-id"), baseline_id, S8("--candidate-id"), candidate_id,
    };
    String8 label = string_format(arena, S8("benchmark-{S8}"), name);
    ProductionProfileCommandResult measured = production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
        context->evidence, label, context->options.timeout_seconds, false);
    if (!measured.success)
    {
        return false;
    }

    String8 compare[] = {context->driver, S8("bench_throughput"), S8("compare"), S8("--output"), directory};
    label = string_format(arena, S8("compare-{S8}"), name);
    return production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(compare), (SliceString8){0}, (SliceString8){0},
        context->evidence, label, context->options.timeout_seconds, false).success;
}

BUSTER_GLOBAL_LOCAL ProcessResult production_profile_main(Arena* arena, SliceString8 arguments, String8 driver)
{
#if !(BUSTER_LINUX || BUSTER_APPLE)
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    BUSTER_UNUSED(driver);
    string_print(S8("error: production_profile currently requires Linux or macOS\n"));
    return PROCESS_RESULT_FAILED;
#else
    ProductionProfileOptions options = {0};
    if (!production_profile_options(arena, arguments, &options))
    {
        if (!arguments.length || !string_equal(arguments.pointer[0], S8("--help")))
        {
            production_profile_usage();
        }
        return PROCESS_RESULT_FAILED;
    }

    ProductionProfileContext context = {.arena = arena, .options = options};
    make_directory_recursive(arena, S8("build"));
    context.repository = os_path_absolute(arena, S8("."), true);
    context.driver = os_path_absolute(arena, driver, true);
    context.output = os_path_absolute_lexical(arena, options.output, true);
    String8 build_root = os_path_absolute(arena, S8("build"), true);
    if (!context.repository.length || !context.driver.length || !context.output.length || !build_root.length ||
        !production_profile_path_components_safe(context.output) ||
        !production_profile_path_is_child(build_root, context.output))
    {
        string_print(S8("error: production_profile output must be a dot-component-free child of the checkout build directory\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 git = production_profile_resolve_tool(arena, S8("git"), S8("git"));
    context.clang = production_profile_resolve_tool(arena, options.clang, S8("Clang"));
    context.llvm_profdata = production_profile_resolve_tool(arena, options.llvm_profdata, S8("llvm-profdata"));
    context.llvm_readobj = production_profile_resolve_tool(arena, options.llvm_readobj, S8("llvm-readobj"));
    if (!git.length || !context.clang.length || !context.llvm_profdata.length || !context.llvm_readobj.length)
    {
        return PROCESS_RESULT_FAILED;
    }

    String8 status_command[] = {
        git, S8("status"), S8("--porcelain=v1"), S8("--untracked-files=all"), S8("--"), S8("."),
        S8(":(exclude)build"), S8(":(exclude)build/**"),
    };
    ProductionProfileCommandResult status = production_profile_capture(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(status_command));
    if (!status.success || production_profile_trim(status.output).length)
    {
        if (status.output.length)
        {
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(status.output));
        }
        string_print(S8("error: production_profile requires a clean source tree outside build/\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 revision_command[] = {git, S8("rev-parse"), S8("--verify"), S8("HEAD")};
    String8 tree_command[] = {git, S8("rev-parse"), S8("--verify"), S8("HEAD^{tree}")};
    ProductionProfileCommandResult revision = production_profile_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(revision_command));
    ProductionProfileCommandResult tree = production_profile_capture(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(tree_command));
    context.revision = production_profile_trim(revision.output);
    context.tree = production_profile_trim(tree.output);
    if (!revision.success || !tree.success || context.revision.length != 40 || context.tree.length != 40)
    {
        string_print(S8("error: could not identify the source revision and tree\n"));
        return PROCESS_RESULT_FAILED;
    }

    if (path_exists(arena, context.output))
    {
        if (!options.clean)
        {
            string_print(S8("error: production_profile output already exists; choose a fresh path or pass --clean\n"));
            return PROCESS_RESULT_FAILED;
        }
        if (!os_directory_delete(context.output))
        {
            string_print(S8("error: could not remove the previous production_profile output\n"));
            return PROCESS_RESULT_FAILED;
        }
    }
    make_directory_recursive(arena, context.output);
    context.evidence = path_join(arena, context.output, S8("evidence"));
    make_directory_recursive(arena, context.evidence);
    make_directory_recursive(arena, path_join(arena, context.output, S8("profile")));

    String8 clang_version_command[] = {context.clang, S8("--version")};
    String8 clang_target_command[] = {context.clang, S8("-dumpmachine")};
    String8 profdata_version_command[] = {context.llvm_profdata, S8("--version")};
    ProductionProfileCommandResult clang_version = production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_version_command), (SliceString8){0}, (SliceString8){0},
        context.evidence, S8("clang-version"), 120, true);
    ProductionProfileCommandResult clang_target = production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_target_command), (SliceString8){0}, (SliceString8){0},
        context.evidence, S8("clang-target"), 120, true);
    ProductionProfileCommandResult profdata_version = production_profile_command(
        arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(profdata_version_command), (SliceString8){0}, (SliceString8){0},
        context.evidence, S8("llvm-profdata-version"), 120, true);

    String8 clang_sha = production_profile_sha256_file(arena, context.clang);
    String8 profdata_sha = production_profile_sha256_file(arena, context.llvm_profdata);
    String8 clang_identity_sha = production_profile_sha256_text(arena, clang_version.output);
    String8 clang_target_sha = production_profile_sha256_text(arena, production_profile_trim(clang_target.output));
    String8 profdata_identity_sha = production_profile_sha256_text(arena, profdata_version.output);
    if (!clang_version.success || !clang_target.success || !profdata_version.success ||
        clang_sha.length != 64 || profdata_sha.length != 64)
    {
        string_print(S8("error: could not seal the LLVM toolchain identity\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 contract = string_format(arena, S8(
        BUSTER_PRODUCTION_PROFILE_CONTRACT_VERSION "\n"
        "source_revision={S8}\n"
        "source_tree={S8}\n"
        "clang_sha256={S8}\n"
        "clang_identity_sha256={S8}\n"
        "clang_target_sha256={S8}\n"
        "llvm_profdata_sha256={S8}\n"
        "llvm_profdata_identity_sha256={S8}\n"
        "training_profile=ci\n"
        "training_mode=all\n"
        "training_pairs=1\n"
        "training_warmups=1\n"
        "training_artifact=object\n"
        "build_debug_info=OFF\n"
        "build_frame_pointers=ON\n"
        "build_lto=OFF\n"),
        context.revision, context.tree, clang_sha, clang_identity_sha, clang_target_sha,
        profdata_sha, profdata_identity_sha);
    context.fingerprint = production_profile_sha256_text(arena, contract);
    if (!production_profile_write(path_join(arena, context.output, S8("profile/training-contract.txt")), contract))
    {
        string_print(S8("error: could not write the training contract\n"));
        return PROCESS_RESULT_FAILED;
    }

    context.profile = path_join(arena, context.output, S8("profile/merged.profdata"));
    context.manifest = path_join(arena, context.output, S8("profile/manifest.txt"));

    ProductionProfileVariant variants_before_profile[] = {
        {.name = S8("release"), .debug_info = true},
        {.name = S8("g0")},
        {.name = S8("lto"), .lto = true},
        {.name = S8("instrumented"), .generate = true},
    };
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(variants_before_profile); index += 1)
    {
        if (!production_profile_build_variant(&context, variants_before_profile[index]))
        {
            return PROCESS_RESULT_FAILED;
        }
    }

    String8 instrumented = production_profile_binary(arena, path_join(arena, context.output, S8("instrumented")));
    if (!production_profile_train(&context, instrumented) || !production_profile_merge(&context))
    {
        return PROCESS_RESULT_FAILED;
    }

    String8 profile_sha = production_profile_sha256_file(arena, context.profile);
    if (profile_sha.length != 64)
    {
        string_print(S8("error: merged profile is missing or empty\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 manifest = string_format(arena, S8(
        BUSTER_PRODUCTION_PROFILE_MANIFEST_VERSION "\n"
        "fingerprint={S8}\n"
        "profile_sha256={S8}\n"
        "source_revision={S8}\n"
        "source_tree={S8}\n"
        "clang_sha256={S8}\n"
        "llvm_profdata_sha256={S8}\n"
        "training_contract_sha256={S8}\n"
        "training_profile=ci\n"
        "training_mode=all\n"
        "training_pairs=1\n"
        "training_warmups=1\n"),
        context.fingerprint, profile_sha, context.revision, context.tree, clang_sha, profdata_sha, context.fingerprint);
    if (!production_profile_write(context.manifest, manifest))
    {
        string_print(S8("error: could not write the profile manifest\n"));
        return PROCESS_RESULT_FAILED;
    }

    ProductionProfileVariant variants_after_profile[] = {
        {.name = S8("pgo"), .use = true},
        {.name = S8("pgo-lto"), .lto = true, .use = true},
    };
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(variants_after_profile); index += 1)
    {
        if (!production_profile_build_variant(&context, variants_after_profile[index]))
        {
            return PROCESS_RESULT_FAILED;
        }
    }

    String8 release = production_profile_binary(arena, path_join(arena, context.output, S8("release")));
    String8 g0 = production_profile_binary(arena, path_join(arena, context.output, S8("g0")));
    String8 lto = production_profile_binary(arena, path_join(arena, context.output, S8("lto")));
    String8 pgo = production_profile_binary(arena, path_join(arena, context.output, S8("pgo")));
    String8 pgo_lto = production_profile_binary(arena, path_join(arena, context.output, S8("pgo-lto")));
    if (!production_profile_validate_binary(&context, pgo_lto))
    {
        return PROCESS_RESULT_FAILED;
    }

    if (options.benchmark)
    {
        struct
        {
            String8 name;
            String8 binary;
        } comparisons[] = {
            {S8("g0"), g0},
            {S8("lto"), lto},
            {S8("pgo"), pgo},
            {S8("pgo-lto"), pgo_lto},
        };
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(comparisons); index += 1)
        {
            if (!production_profile_benchmark_one(&context, release, comparisons[index].binary, comparisons[index].name))
            {
                return PROCESS_RESULT_FAILED;
            }
        }
    }

    String8 final_sha = production_profile_sha256_file(arena, pgo_lto);
    String8 summary = string_format(arena, S8(
        "status=passed\n"
        "source_revision={S8}\n"
        "source_tree={S8}\n"
        "fingerprint={S8}\n"
        "profile={S8}\n"
        "profile_sha256={S8}\n"
        "manifest={S8}\n"
        "production_compiler={S8}\n"
        "production_compiler_sha256={S8}\n"
        "debug_sections=absent\n"
        "correctness=test_all\n"
        "deterministic_outputs=throughput-require-identical-output\n"
        "benchmarks={S8}\n"),
        context.revision, context.tree, context.fingerprint, context.profile, profile_sha, context.manifest,
        pgo_lto, final_sha, options.benchmark ? S8("complete") : S8("skipped"));
    if (!production_profile_write(path_join(arena, context.output, S8("summary.txt")), summary))
    {
        string_print(S8("error: could not write the production-profile summary\n"));
        return PROCESS_RESULT_FAILED;
    }

    string_print(S8("PRODUCTION_PROFILE result=pass compiler={S8} manifest={S8} summary={S8}\n"),
        pgo_lto, context.manifest, path_join(arena, context.output, S8("summary.txt")));
    return PROCESS_RESULT_SUCCESS;
#endif
}

BUSTER_GLOBAL_LOCAL ProcessResult production_profile_self_test(Arena* arena)
{
    u64 failures = 0;
    u64 value = 0;
    failures += !production_profile_u64(S8("0"), &value) || value != 0;
    failures += !production_profile_u64(S8("18446744073709551615"), &value) || value != UINT64_MAX;
    failures += production_profile_u64(S8("18446744073709551616"), &value);
    failures += production_profile_u64(S8("-1"), &value);
    failures += production_profile_u64((String8){0}, &value);
    failures += !production_profile_debug_sections(S8("Name: .debug_info\n"));
    failures += !production_profile_debug_sections(S8("Segment: __DWARF\nName: __debug_line\n"));
    failures += production_profile_debug_sections(S8("Name: .text\nName: .symtab\nName: .llvm_addrsig\n"));
    String8 contract = S8(BUSTER_PRODUCTION_PROFILE_CONTRACT_VERSION "\nsource_revision=abc\n");
    String8 first = production_profile_sha256_text(arena, contract);
    String8 second = production_profile_sha256_text(arena, contract);
    String8 third = production_profile_sha256_text(arena, S8(BUSTER_PRODUCTION_PROFILE_CONTRACT_VERSION "\nsource_revision=abd\n"));
    failures += first.length != 64 || !string_equal(first, second) || string_equal(first, third);
    failures += !production_profile_path_is_child(S8("/checkout"), S8("/checkout/build/profile"));
    failures += production_profile_path_is_child(S8("/checkout"), S8("/checkout-other"));
    failures += !production_profile_path_components_safe(S8("/checkout/build/profile"));
    failures += production_profile_path_components_safe(S8("/checkout/build/../src"));
    failures += production_profile_path_components_safe(S8("/checkout/build/./profile"));

    failures += production_profile_environment_key_equal(S8("PATH"), S8("HOME"));
#if BUSTER_WINDOWS
    failures += !production_profile_environment_key_equal(S8("Path"), S8("PATH"));
#else
    failures += production_profile_environment_key_equal(S8("Path"), S8("PATH"));
#endif

    String8 inherited_keys[] = {S8("PATH"), S8("LLVM_PROFILE_FILE"), S8("HOME")};
    String8 inherited_values[] = {S8("/bin"), S8("old.profraw"), S8("/home/runner")};
    String8 override_keys[] = {S8("LLVM_PROFILE_FILE"), S8("EMPTY")};
    String8 override_values[] = {S8("new-%p.profraw"), (String8){0}};
    SliceString8 merged_keys = {0};
    SliceString8 merged_values = {0};
    bool merged = production_profile_environment_merge(
        arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(inherited_keys),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(inherited_values),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(override_keys),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(override_values),
        &merged_keys, &merged_values);
    failures += !merged || merged_keys.length != 4 || merged_values.length != 4;
    if (merged && merged_keys.length == 4 && merged_values.length == 4)
    {
        failures += !string_equal(merged_keys.pointer[0], S8("PATH")) || !string_equal(merged_values.pointer[0], S8("/bin"));
        failures += !string_equal(merged_keys.pointer[1], S8("HOME")) || !string_equal(merged_values.pointer[1], S8("/home/runner"));
        failures += !string_equal(merged_keys.pointer[2], S8("LLVM_PROFILE_FILE")) || !string_equal(merged_values.pointer[2], S8("new-%p.profraw"));
        failures += !string_equal(merged_keys.pointer[3], S8("EMPTY")) || merged_values.pointer[3].length != 0;
    }

    ProductionProfileOptions options = {0};
    String8 valid_arguments[] = {
        S8("--output"), S8("build/pgo"), S8("--benchmark-profile"), S8("ci"),
        S8("--jobs"), S8("3"), S8("--pairs"), S8("4"), S8("--warmups"), S8("0"),
        S8("--timeout"), S8("90"), S8("--clean"), S8("--no-benchmark"),
    };
    failures += !production_profile_options(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(valid_arguments), &options);
    failures += !string_equal(options.output, S8("build/pgo")) || !string_equal(options.benchmark_profile, S8("ci")) ||
                options.jobs != 3 || options.pairs != 4 || options.warmups != 0 || options.timeout_seconds != 90 ||
                !options.clean || options.benchmark;
    String8 invalid_arguments[] = {S8("--pairs"), S8("0")};
    failures += production_profile_options(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_arguments), &options);

    string_print(S8("PRODUCTION_PROFILE_SELF_TEST failures={u64} result={S8}\n"), failures, failures ? S8("fail") : S8("pass"));
    return failures ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
}
