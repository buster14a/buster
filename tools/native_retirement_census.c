#include "native_retirement_dependency_binding.generated.h"

// Included by build.c after differential.c: this is build-driver orchestration,
// not a second executable. nrc_inventory freezes tracked test inputs and records
// exclusions; nrc_manifest freezes the complete cross product before execution;
// nrc_load_applicability authenticates the immutable fixture/target source
// projection; nrc_group compares a direct baseline with strict MIR using
// bounded d_observe children or emits ledger-bound non-executed control rows.
// Opt-in CODEGEN_FALLBACK_FUNCTION rows attribute every observed fallback;
// fatal diagnostics and aggregate counters are retained too. This is object
// coverage with a reviewed input/dependency/environment contract, never
// execution or final retirement acceptance.

#define NRC_ALLOCATOR(name, value) S8_INITIALIZER(name),
BUSTER_GLOBAL_LOCAL String8 const nrc_allocators[] = {BUSTER_CODEGEN_ALLOCATORS(NRC_ALLOCATOR)};
#undef NRC_ALLOCATOR

typedef struct NrcTarget NrcTarget;
struct NrcTarget { String8 triple; String8 abi; String8 link_obligation; String8 execution_obligation; };
BUSTER_GLOBAL_LOCAL NrcTarget const nrc_targets[] = {
    {S8_INITIALIZER("x86_64-unknown-linux-gnu"), S8_INITIALIZER("systemv-x86_64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("aarch64-unknown-linux-gnu"), S8_INITIALIZER("aapcs64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("x86_64-pc-windows-msvc"), S8_INITIALIZER("win64-x86_64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("aarch64-pc-windows-msvc"), S8_INITIALIZER("windows-aarch64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("x86_64-apple-macos"), S8_INITIALIZER("systemv-x86_64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("aarch64-apple-macos"), S8_INITIALIZER("darwin-aarch64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("x86_64-linux-android"), S8_INITIALIZER("systemv-x86_64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("aarch64-linux-android"), S8_INITIALIZER("aapcs64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("x86_64-apple-ios"), S8_INITIALIZER("systemv-x86_64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("unavailable-platform-control")},
    {S8_INITIALIZER("aarch64-apple-ios"), S8_INITIALIZER("darwin-aarch64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("x86_64-unknown-uefi"), S8_INITIALIZER("win64-x86_64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
    {S8_INITIALIZER("aarch64-unknown-uefi"), S8_INITIALIZER("aapcs64"), S8_INITIALIZER("semantic-gate-509"), S8_INITIALIZER("semantic-gate-509")},
};

BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_manifest_name = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_POLICY_PATH);
BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_snapshot_name = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_SNAPSHOT_PATH);
BUSTER_GLOBAL_LOCAL u64 const nrc_full_input_count = 559;
BUSTER_GLOBAL_LOCAL u64 const nrc_full_subject_count = 411;
BUSTER_GLOBAL_LOCAL u64 const nrc_full_group_count = 19728;
BUSTER_GLOBAL_LOCAL u64 const nrc_full_row_count = 78912;
BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_descriptor_sha256 = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_POLICY_SHA256);
BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_snapshot_sha256 = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_SNAPSHOT_SHA256);
BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_receipt_sha256 = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_RECEIPT_SHA256);
BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_project_sha256 = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_PROJECT_SHA256);
BUSTER_GLOBAL_LOCAL String8 const nrc_dependency_ledger_sha256 = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_LEDGER_SHA256);
BUSTER_GLOBAL_LOCAL String8 const nrc_archived_input_sha256 = S8_INITIALIZER("bef841ade0921ffe9293440171b1d0d8dd6c3cf798f2535d8790b4ad26542500");
BUSTER_GLOBAL_LOCAL String8 const nrc_archived_fixture_map_sha256 = S8_INITIALIZER("8d79504f67d48fd27698c6897b00fc9347dd60a538a6198e53e42970c799bc4f");
BUSTER_GLOBAL_LOCAL String8 const nrc_archived_row_sha256 = S8_INITIALIZER("9604102b75a14631aeb1d6a3652d36506a05928a0046c52cc50a00b942826ce6");
BUSTER_GLOBAL_LOCAL String8 const nrc_applicability_ledger_sha256 = S8_INITIALIZER("934be981e866fe3dbbdb4a5b9e551c052b4546487bb04245fac24bb271be78fa");
BUSTER_GLOBAL_LOCAL u64 const nrc_applicability_ledger_count = 374;

typedef struct NrcInput NrcInput;
struct NrcInput { String8 path; String8 role; String8 compile_obligation; String8 sha256; u64 hash; u64 bytes; };
typedef struct NrcApplicability NrcApplicability;
struct NrcApplicability { String8 fixture; String8 target; String8 fixture_sha256; String8 applicability; String8 reason; };
typedef struct NrcFixtureRecipe NrcFixtureRecipe;
struct NrcFixtureRecipe { String8 name; String8 flags[3]; u32 count; String8 x86_cpu; };
typedef struct NrcStatistics NrcStatistics;
struct NrcStatistics { String8 cpu; String8 features; u32 functions; u32 fallbacks; bool valid; };
typedef struct NrcDependency NrcDependency;
struct NrcDependency { String8 relative; String8 sha256; u64 bytes; };
typedef struct NrcSettings NrcSettings;
struct NrcSettings
{
    DSettings child;
    String8 baseline;
    String8 compiler_revision;
    String8 baseline_revision;
    String8 compiler_sha256;
    String8 baseline_sha256;
    String8 fixture_filter;
    String8 target_filter;
    String8 cpu;
    String8 snapshot;
    String8 resource_include;
    String8 resource_snapshot;
    String8 project_include;
    String8 project_snapshot;
    String8 dependency_manifest;
    String8 dependency_snapshot;
    String8 dependency_receipt;
    String8 dependency_manifest_sha256;
    String8 dependency_snapshot_sha256;
    String8 dependency_resolved_descriptor_sha256;
    String8 dependency_receipt_sha256;
    String8 dependency_project_sha256;
    String8 dependency_ledger_sha256;
    String8 contract_path;
    String8 supported_gap_ledger_path;
    String8 contract_sha256;
    String8 supported_gap_ledger_sha256;
    String8 applicability_ledger_path;
    String8 applicability_ledger_sha256;
    NrcApplicability* applicability;
    u64 applicability_count;
    String8 resource_sha256;
    String8 project_sha256;
    FILE* rows;
    FILE* counters;
    FILE* functions;
    FILE* skips;
    u32 shard_index;
    u32 shard_count;
    u64 selected_groups;
    u64 baseline_failures;
    u64 gaps;
    u64 failures;
    u64 strict_successes;
    u64 strict_empty;
    u64 skipped_rows;
    bool manifest_only;
};

BUSTER_GLOBAL_LOCAL bool nrc_field_safe(String8 value)
{
    bool valid = value.length != 0;
    for (u64 index = 0; index < value.length; index += 1)
    {
        u8 byte = (u8)value.pointer[index];
        valid &= byte >= 32 && byte != 127;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_revision_valid(String8 revision)
{
    bool valid = revision.length == 40;
    for (u64 index = 0; index < revision.length; index += 1)
    {
        char8 byte = revision.pointer[index];
        valid &= (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 nrc_sha256(Arena* arena, u8* bytes, u64 length)
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, length);
    char8* result = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
    sha256_finish_hex(&hash, result);
    return (String8){.pointer = result, .length = SHA256_HEX_CAPACITY - 1};
}

BUSTER_GLOBAL_LOCAL bool nrc_file_identity(Arena* arena, String8 path, u64* bytes_out, u64* hash_out, String8* sha256_out)
{
    FileMapRead map = file_map_read(arena, path, (FileReadOptions){.map_required = 1});
    bool valid = map.mapped_pointer && map.bytes.pointer;
    if (valid)
    {
        *bytes_out = map.bytes.length;
        *hash_out = buster_hash_64(map.bytes.pointer, map.bytes.length);
        *sha256_out = nrc_sha256(arena, map.bytes.pointer, map.bytes.length);
    }
    file_map_unmap(map);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_receipt_field(Arena* arena, ByteSlice bytes, String8 key, String8* value)
{
    String8 text = BYTE_SLICE_TO_STRING(8, bytes);
    String8 prefix = string_format(arena, S8("  \"{S8}\": \""), key);
    String8 line = {0};
    bool found = false;
    while (text_next_line(&text, &line))
    {
        if (!found && line.length >= prefix.length && memory_compare(line.pointer, prefix.pointer, prefix.length))
        {
            u64 end = line.length;
            bool comma = end && line.pointer[end - 1] == ',';
            u64 quote = comma ? end - 2 : end - 1;
            bool quoted = end >= prefix.length + 2 && line.pointer[quote] == '"';
            if (quoted)
            {
                *value = string_slice(line, prefix.length, quote);
                found = true;
            }
        }
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool nrc_sha256_valid(String8 value)
{
    bool valid = value.length == SHA256_HEX_CAPACITY - 1;
    for (u64 index = 0; index < value.length; index += 1)
    {
        char8 byte = value.pointer[index];
        valid &= (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_u64_decimal(String8 value, u64* result)
{
    bool valid = value.length != 0;
    u64 parsed = 0;
    for (u64 index = 0; valid && index < value.length; index += 1)
    {
        u64 digit = (u64)(u8)value.pointer[index] - '0';
        valid = digit <= 9 && parsed <= (UINT64_MAX - digit) / 10;
        if (valid) { parsed = parsed * 10 + digit; }
    }
    if (valid) { *result = parsed; }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_contract_row(String8* text, NrcInput* input)
{
    String8 line = {0};
    bool valid = text_next_line(text, &line) && line.length != 0;
    String8 fields[5] = {0};
    u32 field = 0;
    u64 start = 0;
    for (u64 index = 0; valid && index <= line.length; index += 1)
    {
        if (index == line.length || line.pointer[index] == '\t')
        {
            valid = field < BUSTER_ARRAY_LENGTH(fields) && index > start;
            if (valid) { fields[field++] = string_slice(line, start, index); }
            start = index + 1;
        }
    }
    u64 bytes = 0;
    valid &= field == BUSTER_ARRAY_LENGTH(fields) && nrc_u64_decimal(fields[3], &bytes) && nrc_sha256_valid(fields[4]);
    if (valid)
    {
        *input = (NrcInput){.path = fields[0], .role = fields[1], .compile_obligation = fields[2], .sha256 = fields[4], .bytes = bytes};
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_applicability_class_valid(String8 value)
{
    return string_equal(value, S8("admitted-supported")) ||
           string_equal(value, S8("platform-inapplicable")) ||
           string_equal(value, S8("unavailable"));
}

BUSTER_GLOBAL_LOCAL bool nrc_reason_valid(String8 value)
{
    bool valid = value.length != 0;
    for (u64 index = 0; index < value.length; index += 1)
    {
        char8 byte = value.pointer[index];
        valid &= (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                 (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.';
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_applicability_row(String8* text, NrcApplicability* record)
{
    String8 line = {0};
    bool valid = text_next_line(text, &line) && line.length != 0;
    String8 fields[5] = {0};
    u32 field = 0;
    u64 start = 0;
    for (u64 index = 0; valid && index <= line.length; index += 1)
    {
        if (index == line.length || line.pointer[index] == '\t')
        {
            valid = field < BUSTER_ARRAY_LENGTH(fields) && index > start;
            if (valid) { fields[field++] = string_slice(line, start, index); }
            start = index + 1;
        }
    }
    valid &= field == BUSTER_ARRAY_LENGTH(fields) && nrc_sha256_valid(fields[2]) &&
             nrc_applicability_class_valid(fields[3]) && nrc_reason_valid(fields[4]);
    if (valid)
    {
        *record = (NrcApplicability){.fixture = fields[0], .target = fields[1], .fixture_sha256 = fields[2],
                                     .applicability = fields[3], .reason = fields[4]};
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_load_applicability(NrcSettings* settings, ByteSlice bytes,
                                                NrcInput* inputs, u64 input_count)
{
    Arena* arena = settings->child.arena;
    String8 text = BYTE_SLICE_TO_STRING(8, bytes);
    String8 header = {0};
    bool valid = bytes.pointer && text_next_line(&text, &header) &&
                 string_equal(header, S8("fixture\ttarget\tfixture_sha256\tapplicability\treason"));
    valid &= string_equal(nrc_sha256(arena, bytes.pointer, bytes.length), nrc_applicability_ledger_sha256);
    NrcApplicability* records = arena_allocate(arena, NrcApplicability, nrc_applicability_ledger_count);
    u64 count = 0;
    while (valid && text.length)
    {
        valid &= count < nrc_applicability_ledger_count;
        NrcApplicability record = {0};
        valid &= nrc_applicability_row(&text, &record);
        if (valid)
        {
            bool fixture_found = false;
            for (u64 index = 0; index < input_count; index += 1)
            {
                if (string_equal(inputs[index].path, record.fixture))
                {
                    fixture_found = true;
                    valid &= string_equal(inputs[index].role, S8("subject")) &&
                             string_equal(inputs[index].sha256, record.fixture_sha256);
                }
            }
            bool target_found = false;
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(nrc_targets); index += 1)
            {
                target_found |= string_equal(nrc_targets[index].triple, record.target);
            }
            valid &= fixture_found && target_found;
            if (valid && count)
            {
                NrcApplicability* previous = records + count - 1;
                valid &= assembly_import_string_compare(&previous->fixture, &record.fixture) < 0 ||
                         (string_equal(previous->fixture, record.fixture) &&
                          assembly_import_string_compare(&previous->target, &record.target) < 0);
            }
            if (valid) { records[count++] = record; }
        }
    }
    valid &= count == nrc_applicability_ledger_count && !text.length;
    if (valid)
    {
        settings->applicability = records;
        settings->applicability_count = count;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL NrcApplicability* nrc_applicability_lookup(NrcSettings* settings, String8 fixture, String8 target)
{
    NrcApplicability* result = 0;
    for (u64 index = 0; index < settings->applicability_count; index += 1)
    {
        NrcApplicability* record = settings->applicability + index;
        if (string_equal(record->fixture, fixture) && string_equal(record->target, target)) { result = record; break; }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 nrc_role(String8 path)
{
    // Exact rejection-fixture identities, not keyword matching: for example,
    // basic_c_negative_constant_widening.c is a positive compiler regression.
    String8 rejected[] = {
        S8("tests/basic_c_invalid_labels.c"), S8("tests/basic_c_invalid_asm_goto.c"),
        S8("tests/basic_c_invalid_bit_field_width.c"), S8("tests/basic_c_preprocessor_error.c"),
        S8("tests/self_host_bootstrap_invalid.c"), S8("tests/differential/reject_syntax.c"),
        S8("tests/differential/reject_type.c"),
        S8("tests/basic_c_bit_field_alignas.c"), S8("tests/basic_c_function_pointer_conflict.c"),
        S8("tests/basic_c_asm_literal_register.c"), S8("tests/basic_c_sizeof_missing_member.c"),
        S8("tests/basic_c_sizeof_parenthesized_type.c"),
    };
    String8 result = string_ends_with_sequence(path, S8(".c")) ? S8("subject") : S8("support-file");
    if (string_ends_with_sequence(path, S8(".bbb"))) { result = S8("dormant-custom-language"); }
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(rejected); index += 1)
    {
        if (string_equal(path, rejected[index])) { result = S8("negative-diagnostic-fixture"); }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 nrc_compile_obligation(String8 path, String8 role)
{
    String8 result = S8("dependency-only");
    if (string_equal(role, S8("subject")))
    {
        result = S8("supported-object-zero-fallback");
        String8 non_object_controls[] = {
            S8("tests/basic_c_macro_options.c"), S8("tests/ebpf_scalar_regression.c"),
            S8("tests/runtime_boundary_regression.c"), S8("tests/wasm_memory_alignment_regression.c"),
            S8("tests/windows_unicode_regression.c"), S8("tests/gpu/metal_reader.c"),
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(non_object_controls); index += 1)
        {
            if (string_equal(path, non_object_controls[index])) { result = S8("registered-non-object-control"); }
        }
    }
    else if (string_equal(role, S8("negative-diagnostic-fixture"))) { result = S8("registered-rejection-control"); }
    else if (string_equal(role, S8("dormant-custom-language"))) { result = S8("preserved-not-active"); }
    return result;
}

BUSTER_GLOBAL_LOCAL NrcFixtureRecipe nrc_fixture_recipe(String8 path)
{
    // Match known C23 inputs and the registered C23 dialect assertion variant.
    // Similar names retain their own defaults; no source-text guessing occurs.
    String8 c23[] = {S8("tests/basic_c_constexpr.c"), S8("tests/basic_c_constexpr_leaf.c"),
                    S8("tests/basic_c_typeof.c"), S8("tests/basic_c_nullptr.c")};
    NrcFixtureRecipe recipe = {.name = S8("compiler-default")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c23); index += 1)
    {
        if (string_equal(path, c23[index]))
        {
            recipe = (NrcFixtureRecipe){.name = S8("c23"), .flags = {S8("-std=c23")}, .count = 1};
        }
    }
    if (string_equal(path, S8("tests/basic_c_dialect.c")))
    {
        recipe = (NrcFixtureRecipe){.name = S8("c23-dialect-assertions"),
            .flags = {S8("-std=c23"), S8("-DEXPECTED_STDC_VERSION=202311L"), S8("-DEXPECTED_GNU=0")}, .count = 3};
    }
    else if (string_equal(path, S8("tests/basic_c_predicate_bank.c")))
    {
        recipe.name = S8("x86-avx512");
        recipe.x86_cpu = S8("skylake-avx512");
    }
    else if (string_equal(path, S8("tests/basic_c_atomic_aggregate.c")))
    {
        recipe.name = S8("x86-cx16");
        recipe.x86_cpu = S8("haswell");
    }
    return recipe;
}

// Hosted library probes need the target SDK, not the Linux runner's libc.
BUSTER_GLOBAL_LOCAL bool nrc_hosted_fixture(String8 path)
{
    String8 fixtures[] = {
        S8("tests/basic_c_target_headers.c"), S8("tests/basic_cjson_roundtrip.c"),
        S8("tests/basic_doom_headless.c"), S8("tests/basic_lz4_roundtrip.c"),
        S8("tests/basic_stb_compat.c"), S8("tests/basic_yyjson_roundtrip.c"),
        S8("tests/basic_zlib_compat.c"),
    };
    bool result = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fixtures); index += 1)
    {
        result |= string_equal(path, fixtures[index]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 nrc_recipe_cpu(NrcFixtureRecipe recipe, NrcTarget target, String8 fallback)
{
    String8 result = fallback;
    if (recipe.x86_cpu.length && string_starts_with_sequence(target.triple, S8("x86_64-")))
    {
        result = recipe.x86_cpu;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL FILE* nrc_open(NrcSettings* settings, String8 name)
{
    String8 path = path_join(settings->child.arena, settings->child.out, name);
    FILE* file = fopen((char*)path.pointer, "wb");
    settings->child.io_failed |= file == 0;
    return file;
}

BUSTER_GLOBAL_LOCAL void nrc_close(NrcSettings* settings, FILE* file)
{
    if (file)
    {
        settings->child.io_failed |= ferror(file) != 0;
        settings->child.io_failed |= fclose(file) != 0;
    }
}

BUSTER_GLOBAL_LOCAL void nrc_dependency_append(Arena* arena, NrcDependency** dependencies, u64* count, u64* capacity, NrcDependency value)
{
    if (*count == *capacity)
    {
        u64 grown_capacity = *capacity ? *capacity * 2 : 512;
        NrcDependency* grown = arena_allocate(arena, NrcDependency, grown_capacity);
        if (*count) { memcpy(grown, *dependencies, *count * sizeof(*grown)); }
        *dependencies = grown;
        *capacity = grown_capacity;
    }
    (*dependencies)[(*count)++] = value;
}

BUSTER_GLOBAL_LOCAL bool nrc_snapshot_copy(NrcSettings* settings, String8 source, String8 destination,
                                           NrcDependency* dependency, Sha256* closure)
{
    Arena* arena = settings->child.arena;
    ByteSlice bytes = file_read(arena, source, (FileReadOptions){0});
    OsFileDescriptor* descriptor = os_file_open(source, (OpenFlags){0}, (OsFileAccess){ .read = 1 }, (OsFileCreateMode){0}, (OsFileShareFlags){ .read = 1 });
    bool valid = descriptor && bytes.pointer;
    if (descriptor)
    {
        valid &= os_file_get_size(descriptor) == bytes.length;
        valid &= os_file_close(descriptor);
    }
    if (valid)
    {
        dependency->bytes = bytes.length;
        dependency->sha256 = nrc_sha256(arena, bytes.pointer, bytes.length);
        // ``file_read`` and the size probe above intentionally use separate
        // interfaces for the portable driver. Re-check the source identity
        // before publishing non-empty files so a same-size replacement cannot
        // enter the frozen include tree between those probes.
        if (bytes.length)
        {
            u64 source_bytes = 0, source_hash = 0;
            String8 source_sha256 = {0};
            valid = nrc_file_identity(arena, source, &source_bytes, &source_hash, &source_sha256) &&
                    source_bytes == dependency->bytes && source_hash == buster_hash_64(bytes.pointer, bytes.length) &&
                    string_equal(source_sha256, dependency->sha256);
        }
    }
    if (valid)
    {
        make_directory_recursive(arena, path_parent(arena, destination));
        d_write(&settings->child, destination, BYTE_SLICE_TO_STRING(8, bytes));

        // The materializer publishes immutable source bytes, but the census
        // still verifies the copy it is about to put on the compiler's include
        // path. This catches truncation, a failed parent creation, and any
        // output-side race before the closure is admitted to the ledger.
        ByteSlice copied = file_read(arena, destination, (FileReadOptions){0});
        valid &= copied.pointer && copied.length == bytes.length;
        if (valid)
        {
            String8 copied_sha256 = nrc_sha256(arena, copied.pointer, copied.length);
            valid &= string_equal(copied_sha256, dependency->sha256);
        }
        if (valid)
        {
            sha256_add(closure, dependency->relative.pointer, dependency->relative.length);
            sha256_add(closure, "\0", 1);
            u8 encoded_bytes[8];
            for (u32 byte = 0; byte < BUSTER_ARRAY_LENGTH(encoded_bytes); byte += 1)
            {
                encoded_bytes[byte] = (u8)(dependency->bytes >> (byte * 8));
            }
            sha256_add(closure, encoded_bytes, sizeof(encoded_bytes));
            sha256_add(closure, bytes.pointer, bytes.length);
        }
    }
    return valid && !settings->child.io_failed;
}

BUSTER_GLOBAL_LOCAL bool nrc_snapshot_project(NrcSettings* settings, FILE* ledger, Sha256* closure)
{
    Arena* arena = settings->child.arena;
    String8* directories = arena_allocate(arena, String8, 64);
    u64 directory_count = 1, directory_capacity = 64;
    directories[0] = S8("");
    NrcDependency* dependencies = 0;
    u64 dependency_count = 0, dependency_capacity = 0;
    bool valid = settings->project_include.length != 0;
    for (u64 directory_index = 0; valid && directory_index < directory_count; directory_index += 1)
    {
        String8 relative = directories[directory_index];
        String8 source = relative.length ? path_join(arena, settings->project_include, relative) : settings->project_include;
        MuslDirectoryEntry* entries = 0;
        u64 entry_count = 0;
        valid = musl_list_directory(arena, source, &entries, &entry_count);
        for (u64 index = 0; valid && index < entry_count; index += 1)
        {
            String8 child = relative.length ? path_join(arena, relative, entries[index].name) : entries[index].name;
            valid = nrc_field_safe(child);
            if (entries[index].is_directory)
            {
                if (directory_count == directory_capacity)
                {
                    u64 grown_capacity = directory_capacity * 2;
                    String8* grown = arena_allocate(arena, String8, grown_capacity);
                    memcpy(grown, directories, directory_count * sizeof(*grown));
                    directories = grown;
                    directory_capacity = grown_capacity;
                }
                directories[directory_count++] = child;
            }
            else
            {
                nrc_dependency_append(arena, &dependencies, &dependency_count, &dependency_capacity,
                                      (NrcDependency){.relative = child});
            }
        }
    }
    for (u64 index = 1; valid && index < dependency_count; index += 1)
    {
        NrcDependency value = dependencies[index];
        u64 slot = index;
        while (slot && assembly_import_string_compare(&dependencies[slot - 1].relative, &value.relative) > 0)
        {
            dependencies[slot] = dependencies[slot - 1];
            slot -= 1;
        }
        dependencies[slot] = value;
    }
    for (u64 index = 0; valid && index < dependency_count; index += 1)
    {
        NrcDependency* dependency = dependencies + index;
        String8 source = path_join(arena, settings->project_include, dependency->relative);
        String8 destination = path_join(arena, settings->project_snapshot, dependency->relative);
        valid = nrc_snapshot_copy(settings, source, destination, dependency, closure);
        if (valid)
        {
            fprintf(ledger, "project-header\t%.*s\t%llu\t%.*s\n", (int)dependency->relative.length,
                    dependency->relative.pointer, (unsigned long long)dependency->bytes,
                    (int)dependency->sha256.length, dependency->sha256.pointer);
        }
    }
    valid &= dependency_count != 0 && !settings->child.io_failed;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_snapshot_resource(NrcSettings* settings)
{
    Arena* arena = settings->child.arena;
    String8* directories = arena_allocate(arena, String8, 64);
    u64 directory_count = 1, directory_capacity = 64;
    directories[0] = S8("");
    NrcDependency* dependencies = 0;
    u64 dependency_count = 0, dependency_capacity = 0;
    bool valid = settings->resource_include.length != 0;
    for (u64 directory_index = 0; valid && directory_index < directory_count; directory_index += 1)
    {
        String8 relative = directories[directory_index];
        String8 source = relative.length ? path_join(arena, settings->resource_include, relative) : settings->resource_include;
        MuslDirectoryEntry* entries = 0;
        u64 entry_count = 0;
        valid = musl_list_directory(arena, source, &entries, &entry_count);
        for (u64 index = 0; valid && index < entry_count; index += 1)
        {
            String8 child = relative.length ? path_join(arena, relative, entries[index].name) : entries[index].name;
            valid = nrc_field_safe(child);
            if (entries[index].is_directory)
            {
                if (directory_count == directory_capacity)
                {
                    u64 grown_capacity = directory_capacity * 2;
                    String8* grown = arena_allocate(arena, String8, grown_capacity);
                    memcpy(grown, directories, directory_count * sizeof(*grown));
                    directories = grown;
                    directory_capacity = grown_capacity;
                }
                directories[directory_count++] = child;
            }
            else
            {
                nrc_dependency_append(arena, &dependencies, &dependency_count, &dependency_capacity,
                                      (NrcDependency){.relative = child});
            }
        }
    }
    for (u64 index = 1; valid && index < dependency_count; index += 1)
    {
        NrcDependency value = dependencies[index];
        u64 slot = index;
        while (slot && assembly_import_string_compare(&dependencies[slot - 1].relative, &value.relative) > 0)
        {
            dependencies[slot] = dependencies[slot - 1];
            slot -= 1;
        }
        dependencies[slot] = value;
    }
    FILE* ledger = valid ? nrc_open(settings, S8("dependencies.tsv")) : 0;
    Sha256 closure;
    sha256_init(&closure);
    if (ledger) { fprintf(ledger, "kind\tpath\tbytes\tsha256\n"); }
    for (u64 index = 0; valid && ledger && index < dependency_count; index += 1)
    {
        NrcDependency* dependency = dependencies + index;
        String8 source = path_join(arena, settings->resource_include, dependency->relative);
        String8 destination = path_join(arena, settings->resource_snapshot, dependency->relative);
        valid = nrc_snapshot_copy(settings, source, destination, dependency, &closure);
        if (valid)
        {
            fprintf(ledger, "resource-header\t%.*s\t%llu\t%.*s\n", (int)dependency->relative.length,
                    dependency->relative.pointer, (unsigned long long)dependency->bytes,
                    (int)dependency->sha256.length, dependency->sha256.pointer);
        }
    }
    Sha256 project_closure;
    sha256_init(&project_closure);
    bool project_valid = !settings->project_include.length;
    if (valid && ledger && settings->project_include.length)
    {
        project_valid = nrc_snapshot_project(settings, ledger, &project_closure);
    }
    nrc_close(settings, ledger);
    char8* closure_hex = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
    sha256_finish_hex(&closure, closure_hex);
    settings->resource_sha256 = (String8){.pointer = closure_hex, .length = SHA256_HEX_CAPACITY - 1};
    if (settings->project_include.length)
    {
        char8* project_hex = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
        sha256_finish_hex(&project_closure, project_hex);
        settings->project_sha256 = (String8){.pointer = project_hex, .length = SHA256_HEX_CAPACITY - 1};
    }
    valid &= dependency_count != 0 && project_valid && !settings->child.io_failed;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_dependency_binding(NrcSettings* settings, bool required)
{
    Arena* arena = settings->child.arena;
    bool valid = !required;
    if (required && (!settings->dependency_manifest.length || !settings->dependency_snapshot.length ||
                     !settings->dependency_receipt.length))
    {
        valid = false;
    }
    else if (required)
    {
        String8 expected_project_root = path_join(arena, path_parent(arena, settings->dependency_receipt),
                                                  S8("dependencies/project-include"));
        String8 resolved_path = path_join(arena, path_parent(arena, settings->dependency_receipt),
                                          S8("dependency-resolved-descriptor.json"));
        bool project_path_valid = string_equal(settings->project_include, expected_project_root);
        String8 expected_policy_path = os_path_absolute(arena, nrc_dependency_manifest_name, true);
        String8 expected_snapshot_path = os_path_absolute(arena, nrc_dependency_snapshot_name, true);
        bool policy_path_valid = string_equal(settings->dependency_manifest, expected_policy_path);
        bool snapshot_path_valid = string_equal(settings->dependency_snapshot, expected_snapshot_path);
        ByteSlice policy = file_read(arena, settings->dependency_manifest, (FileReadOptions){0});
        ByteSlice snapshot = file_read(arena, settings->dependency_snapshot, (FileReadOptions){0});
        ByteSlice resolved = file_read(arena, resolved_path, (FileReadOptions){0});
        ByteSlice receipt = file_read(arena, settings->dependency_receipt, (FileReadOptions){0});
        u64 policy_bytes = 0, policy_hash = 0, snapshot_bytes = 0, snapshot_hash = 0;
        u64 resolved_bytes = 0, resolved_hash = 0, receipt_bytes = 0, receipt_hash = 0;
        String8 policy_sha256 = {0}, snapshot_sha256 = {0}, resolved_sha256 = {0}, receipt_sha256 = {0};
        valid = project_path_valid && policy_path_valid && snapshot_path_valid && policy.pointer && snapshot.pointer &&
                resolved.pointer && receipt.pointer &&
                nrc_file_identity(arena, settings->dependency_manifest, &policy_bytes, &policy_hash, &policy_sha256) &&
                nrc_file_identity(arena, settings->dependency_snapshot, &snapshot_bytes, &snapshot_hash, &snapshot_sha256) &&
                nrc_file_identity(arena, resolved_path, &resolved_bytes, &resolved_hash, &resolved_sha256) &&
                nrc_file_identity(arena, settings->dependency_receipt, &receipt_bytes, &receipt_hash, &receipt_sha256);
        valid &= string_equal(policy_sha256, nrc_dependency_descriptor_sha256) &&
                 string_equal(snapshot_sha256, nrc_dependency_snapshot_sha256) &&
                 string_equal(receipt_sha256, nrc_dependency_receipt_sha256);
        String8 receipt_descriptor_sha256 = {0}, receipt_project_sha256 = {0};
        String8 receipt_ledger_sha256 = {0}, receipt_path = {0};
        valid &= nrc_receipt_field(arena, receipt, S8("descriptor_sha256"), &receipt_descriptor_sha256) &&
                 nrc_receipt_field(arena, receipt, S8("project_include_sha256"), &receipt_project_sha256) &&
                 nrc_receipt_field(arena, receipt, S8("ledger_sha256"), &receipt_ledger_sha256) &&
                 nrc_receipt_field(arena, receipt, S8("descriptor_path"), &receipt_path);
        valid &= string_equal(receipt_descriptor_sha256, resolved_sha256) &&
                 string_equal(receipt_path, nrc_dependency_manifest_name) &&
                 string_equal(receipt_project_sha256, settings->project_sha256) &&
                 string_equal(settings->project_sha256, nrc_dependency_project_sha256);
        String8 dependency_ledger_path = path_join(arena, path_parent(arena, settings->dependency_receipt), S8("dependencies.tsv"));
        u64 ledger_bytes = 0, ledger_hash = 0;
        String8 ledger_sha256 = {0};
        valid &= nrc_file_identity(arena, dependency_ledger_path, &ledger_bytes, &ledger_hash, &ledger_sha256);
        valid &= string_equal(receipt_ledger_sha256, ledger_sha256) &&
                 string_equal(ledger_sha256, nrc_dependency_ledger_sha256);
        settings->dependency_manifest_sha256 = policy_sha256;
        settings->dependency_snapshot_sha256 = snapshot_sha256;
        settings->dependency_resolved_descriptor_sha256 = resolved_sha256;
        settings->dependency_receipt_sha256 = receipt_sha256;
        settings->dependency_project_sha256 = receipt_project_sha256;
        settings->dependency_ledger_sha256 = ledger_sha256;
        if (policy.pointer)
        {
            d_write(&settings->child, path_join(arena, settings->child.out, S8("dependency-policy.json")),
                    BYTE_SLICE_TO_STRING(8, policy));
        }
        if (snapshot.pointer)
        {
            d_write(&settings->child, path_join(arena, settings->child.out, S8("dependency-source-snapshot.json")),
                    BYTE_SLICE_TO_STRING(8, snapshot));
        }
        if (resolved.pointer)
        {
            d_write(&settings->child, path_join(arena, settings->child.out, S8("dependency-resolved-descriptor.json")),
                    BYTE_SLICE_TO_STRING(8, resolved));
        }
        if (receipt.pointer)
        {
            d_write(&settings->child, path_join(arena, settings->child.out, S8("dependency-receipt.json")),
                    BYTE_SLICE_TO_STRING(8, receipt));
        }
        ByteSlice ledger = file_read(arena, dependency_ledger_path, (FileReadOptions){0});
        if (ledger.pointer)
        {
            d_write(&settings->child, path_join(arena, settings->child.out, S8("dependency-materializer.tsv")),
                    BYTE_SLICE_TO_STRING(8, ledger));
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL void nrc_explicit_environment(NrcSettings* settings)
{
    Arena* arena = settings->child.arena;
    String8* keys = arena_allocate(arena, String8, 6);
    String8* values = arena_allocate(arena, String8, 6);
    u64 count = 0;
    keys[count] = S8("LC_ALL"); values[count++] = S8("C");
    keys[count] = S8("LANG"); values[count++] = S8("C");
    keys[count] = S8("TZ"); values[count++] = S8("UTC");
#if BUSTER_WINDOWS
    String8 temporary = path_join(arena, settings->child.out, S8("temporary"));
    make_directory_recursive(arena, temporary);
    keys[count] = S8("SystemRoot"); values[count++] = os_get_environment_variable(S8("SystemRoot"));
    keys[count] = S8("TEMP"); values[count++] = temporary;
    keys[count] = S8("TMP"); values[count++] = temporary;
#endif
    for (u64 index = 0; index < count; index += 1)
    {
        settings->child.io_failed |= !nrc_field_safe(keys[index]) || !nrc_field_safe(values[index]);
    }
    settings->child.environment_keys = (SliceString8){.pointer = keys, .length = count};
    settings->child.environment_values = (SliceString8){.pointer = values, .length = count};
    settings->child.explicit_environment = true;
    FILE* environment = nrc_open(settings, S8("environment.tsv"));
    if (environment)
    {
        fprintf(environment, "name\tpresent\tvalue\n");
        for (u64 index = 0; index < count; index += 1)
        {
            fprintf(environment, "%.*s\t%u\t%.*s\n", (int)keys[index].length, keys[index].pointer,
                    (unsigned)(values[index].pointer != 0), (int)values[index].length, values[index].pointer);
        }
    }
    nrc_close(settings, environment);
}

BUSTER_GLOBAL_LOCAL NrcInput* nrc_inventory(NrcSettings* settings, u64* count_out)
{
    Arena* arena = settings->child.arena;
    ByteSlice contract_bytes = file_read(arena, settings->contract_path, (FileReadOptions){0});
    String8 contract = BYTE_SLICE_TO_STRING(8, contract_bytes);
    String8 contract_header = {0};
    bool valid = contract_bytes.pointer && text_next_line(&contract, &contract_header) &&
                 string_equal(contract_header, S8("path\trole\tcompile_obligation\tbytes\tsha256"));
    settings->contract_sha256 = valid ? nrc_sha256(arena, contract_bytes.pointer, contract_bytes.length) : (String8){0};
    String8 git = executable_resolve_in_path(arena, S8("git"));
    String8 command[] = {git, S8("ls-files"), S8("-z"), S8("--"), S8("tests")};
    DObservation listed = d_observe(&settings->child, (SliceString8)BUSTER_ARRAY_TO_SLICE(command),
                                    path_join(arena, settings->child.out, S8("tracked-inputs")));
    u64 count = 0;
    for (u64 index = 0; index < listed.output.length; index += 1) { count += listed.output.pointer[index] == 0; }
    valid &= d_success(listed) && count && listed.output.pointer[listed.output.length - 1] == 0;
    NrcInput* inputs = arena_allocate(arena, NrcInput, count);
    u64 from = 0, at = 0;
    for (u64 index = 0; valid && index < listed.output.length; index += 1)
    {
        if (!listed.output.pointer[index])
        {
            String8 path = string_slice(listed.output, from, index);
            valid = nrc_field_safe(path) && string_starts_with_sequence(path, S8("tests/")) && !d_contains(path, S8("/../"));
            if (valid)
            {
                String8 role = nrc_role(path);
                inputs[at++] = (NrcInput){.path = string_duplicate_arena(arena, path, true), .role = role,
                                         .compile_obligation = nrc_compile_obligation(path, role)};
            }
            from = index + 1;
        }
    }
    // Stable insertion sort avoids introducing callback dispatch. The tracked
    // inventory is small and this happens once, outside compiler measurement.
    for (u64 index = 1; valid && index < count; index += 1)
    {
        NrcInput value = inputs[index];
        u64 slot = index;
        while (slot && assembly_import_string_compare(&inputs[slot - 1].path, &value.path) > 0)
        {
            inputs[slot] = inputs[slot - 1];
            slot -= 1;
        }
        inputs[slot] = value;
    }
    FILE* manifest = valid ? nrc_open(settings, S8("inputs.tsv")) : 0;
    if (manifest)
    {
        fprintf(manifest, "path\trole\tcompile_obligation\tbytes\tbuster_hash_64\tsha256\tfixture_recipe\tfixture_flags\n");
        for (u64 index = 0; valid && index < count; index += 1)
        {
            NrcInput* input = inputs + index;
            NrcInput approved = {0};
            valid = nrc_contract_row(&contract, &approved) && path_exists(arena, input->path) &&
                    (index == 0 || !string_equal(input->path, inputs[index - 1].path));
            if (valid)
            {
                TemporalArena temporary = scratch_begin(&arena, 1);
                // Freeze support headers with the translation units; no reads
                // from changing original test paths are needed during rows.
                String8 destination = path_join(temporary.arena, settings->snapshot, input->path);
                make_directory_recursive(temporary.arena, path_parent(temporary.arena, destination));
                ByteSlice bytes = file_read(temporary.arena, input->path, (FileReadOptions){0});
                OsFileDescriptor* source = os_file_open(
                    input->path,
                    (OpenFlags){0},
                    (OsFileAccess){ .read = 1 },
                    (OsFileCreateMode){0},
                    (OsFileShareFlags){ .read = 1 });
                valid = source != 0;
                if (source)
                {
                    valid = bytes.pointer && os_file_get_size(source) == bytes.length;
                    valid &= os_file_close(source);
                }
                input->bytes = bytes.length;
                input->hash = buster_hash_64(bytes.pointer, bytes.length);
                input->sha256 = nrc_sha256(arena, bytes.pointer, bytes.length);
                if (valid) { d_write(&settings->child, destination, BYTE_SLICE_TO_STRING(8, bytes)); }
                if (valid)
                {
                    u64 frozen_bytes = 0, frozen_hash = 0;
                    String8 frozen_sha256 = {0};
                    valid = nrc_file_identity(temporary.arena, destination, &frozen_bytes, &frozen_hash, &frozen_sha256) &&
                            frozen_bytes == input->bytes && frozen_hash == input->hash &&
                            string_equal(frozen_sha256, input->sha256);
                }
                valid &= string_equal(input->path, approved.path) && string_equal(input->role, approved.role) &&
                         string_equal(input->compile_obligation, approved.compile_obligation) && input->bytes == approved.bytes &&
                         string_equal(input->sha256, approved.sha256);
                NrcFixtureRecipe recipe = nrc_fixture_recipe(input->path);
                fprintf(manifest, "%.*s\t%.*s\t%.*s\t%llu\t%llu\t%.*s\t%.*s\t", (int)input->path.length, input->path.pointer,
                        (int)input->role.length, input->role.pointer, (int)input->compile_obligation.length,
                        input->compile_obligation.pointer, (unsigned long long)input->bytes, (unsigned long long)input->hash,
                        (int)input->sha256.length, input->sha256.pointer,
                        (int)recipe.name.length, recipe.name.pointer);
                for (u32 flag = 0; flag < recipe.count; flag += 1)
                {
                    fprintf(manifest, "%s%.*s", flag ? " " : "", (int)recipe.flags[flag].length, recipe.flags[flag].pointer);
                }
                fprintf(manifest, "\n");
                scratch_end(temporary);
            }
        }
        nrc_close(settings, manifest);
    }
    String8 trailing = {0};
    valid &= !text_next_line(&contract, &trailing);
    settings->child.io_failed |= !valid;
    *count_out = valid ? count : 0;
    return inputs;
}

BUSTER_GLOBAL_LOCAL bool nrc_selected(NrcSettings* settings, String8 fixture, String8 target, u64 group)
{
    return (!settings->fixture_filter.length || d_contains(fixture, settings->fixture_filter)) &&
           (!settings->target_filter.length || string_equal(target, settings->target_filter)) &&
           group % settings->shard_count == settings->shard_index;
}

BUSTER_GLOBAL_LOCAL bool nrc_u32_field(String8 line, String8 key, u32* value)
{
    u64 matches = 0;
    bool valid = true;
    u64 offset = 0;
    while (offset < line.length)
    {
        u64 end = offset;
        while (end < line.length && line.pointer[end] != ' ') { end += 1; }
        String8 field_key = {0}, field_value = {0};
        if (text_split_field(string_slice(line, offset, end), &field_key, &field_value) && string_equal(field_key, key))
        {
            matches += 1;
            valid &= d_number(field_value, value);
        }
        offset = end < line.length ? end + 1 : end;
    }
    return valid && matches == 1;
}

BUSTER_GLOBAL_LOCAL bool nrc_string_field(String8 line, String8 key, String8* value);

BUSTER_GLOBAL_LOCAL NrcStatistics nrc_statistics(String8 output, String8 allocator)
{
    NrcStatistics result = {0};
    u32 matches = 0, target_matches = 0;
    bool valid = true;
    String8 line = {0};
    while (text_next_line(&output, &line))
    {
        if (string_starts_with_sequence(line, S8("TARGET ")))
        {
            target_matches += 1;
            valid &= nrc_string_field(line, S8("cpu"), &result.cpu) && nrc_string_field(line, S8("features"), &result.features);
        }
        else if (string_starts_with_sequence(line, S8("CODEGEN ")))
        {
            matches += 1;
            valid &= nrc_u32_field(line, S8("functions"), &result.functions) &&
                     nrc_u32_field(line, S8("fallback_functions"), &result.fallbacks);
            u64 cursor = 0;
            u32 allocator_matches = 0;
            while (cursor < line.length)
            {
                u64 end = cursor;
                while (end < line.length && line.pointer[end] != ' ') { end += 1; }
                String8 field = string_slice(line, cursor, end);
                String8 key = {0}, value = {0};
                if (text_split_field(field, &key, &value) && string_equal(key, S8("allocator")))
                {
                    allocator_matches += 1;
                    valid &= string_equal(value, allocator);
                }
                cursor = end < line.length ? end + 1 : end;
            }
            valid &= allocator_matches == 1 && result.fallbacks <= result.functions;
        }
    }
    result.valid = valid && matches == 1 && target_matches == 1;
    return result;
}

BUSTER_GLOBAL_LOCAL void nrc_counters(NrcSettings* settings, u64 row, String8 output, String8 target, String8 allocator)
{
    String8 line = {0};
    while (text_next_line(&output, &line))
    {
        if (string_starts_with_sequence(line, S8("CODEGEN_FALLBACK")) &&
            !string_starts_with_sequence(line, S8("CODEGEN_FALLBACK_FUNCTION ")) &&
            !string_starts_with_sequence(line, S8("CODEGEN_FALLBACK_CENSUS ")))
        {
            if (string_starts_with_sequence(line, S8("CODEGEN_FALLBACK_REASON ")))
            {
                fprintf(settings->counters, "%llu\tCODEGEN_FALLBACK_REASON %.*s version=1 row=%llu\n",
                        (unsigned long long)row, (int)(line.length - 24), line.pointer + 24, (unsigned long long)row);
            }
            else if (string_starts_with_sequence(line, S8("CODEGEN_FALLBACK opcode=")))
            {
                fprintf(settings->counters, "%llu\t%.*s version=1 row=%llu target=%.*s allocator=%.*s\n",
                        (unsigned long long)row, (int)line.length, line.pointer, (unsigned long long)row,
                        (int)target.length, target.pointer, (int)allocator.length, allocator.pointer);
            }
            else if (string_starts_with_sequence(line, S8("CODEGEN_FALLBACK_STAGES ")))
            {
                fprintf(settings->counters, "%llu\t%.*s version=1 row=%llu target=%.*s allocator=%.*s\n",
                        (unsigned long long)row, (int)line.length, line.pointer, (unsigned long long)row,
                        (int)target.length, target.pointer, (int)allocator.length, allocator.pointer);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL bool nrc_string_field(String8 line, String8 key, String8* value)
{
    u32 matches = 0;
    u64 offset = 0;
    while (offset < line.length)
    {
        u64 end = offset;
        while (end < line.length && line.pointer[end] != ' ') { end += 1; }
        String8 field_key = {0}, field_value = {0};
        if (text_split_field(string_slice(line, offset, end), &field_key, &field_value) && string_equal(field_key, key))
        {
            matches += 1;
            *value = field_value;
        }
        offset = end < line.length ? end + 1 : end;
    }
    return matches == 1 && value->length != 0;
}

BUSTER_GLOBAL_LOCAL bool nrc_hex_field(String8 value)
{
    bool valid = value.length && (value.length % 2 == 0 || string_equal(value, S8("-")));
    if (!string_equal(value, S8("-")))
    {
        for (u64 index = 0; index < value.length; index += 1)
        {
            char8 byte = value.pointer[index];
            valid &= (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool nrc_function_records(NrcSettings* settings, u64 row, String8 output, String8 allocator, u32 expected)
{
    String8 reasons[] = {S8("target-excluded"), S8("signature"), S8("opcode"), S8("selection-other"), S8("verification"),
                        S8("placement"), S8("encoding"), S8("output-capacity"), S8("unwind")};
    u32 summaries = 0, records = 0, declared = 0, previous_function = 0;
    bool valid = true;
    String8 line = {0};
    while (text_next_line(&output, &line))
    {
        u32 version = 0;
        if (string_starts_with_sequence(line, S8("CODEGEN_FALLBACK_CENSUS ")))
        {
            summaries += 1;
            valid &= nrc_u32_field(line, S8("version"), &version) && version == 1 && nrc_u32_field(line, S8("records"), &declared);
        }
        else if (string_starts_with_sequence(line, S8("CODEGEN_FALLBACK_FUNCTION ")))
        {
            u32 function = 0, opcode = 0, source_line = 0, column = 0;
            String8 source = {0}, name = {0}, reason = {0}, stage = {0}, mode = {0}, target = {0};
            bool record_valid = nrc_u32_field(line, S8("version"), &version) && version == 1 &&
                nrc_u32_field(line, S8("function_id"), &function) && nrc_u32_field(line, S8("opcode_id"), &opcode) &&
                nrc_u32_field(line, S8("line"), &source_line) && nrc_u32_field(line, S8("column"), &column) &&
                nrc_string_field(line, S8("source_hex"), &source) && nrc_hex_field(source) &&
                nrc_string_field(line, S8("function_hex"), &name) && nrc_hex_field(name) &&
                nrc_string_field(line, S8("reason"), &reason) && nrc_string_field(line, S8("stage"), &stage) &&
                nrc_string_field(line, S8("target"), &target) && nrc_string_field(line, S8("allocator"), &mode) && string_equal(mode, allocator);
            bool reason_found = false;
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(reasons); index += 1)
            {
                if (string_equal(reason, reasons[index]))
                {
                    reason_found = true;
                    record_valid &= string_equal(stage, index >= 1 && index <= 3 ? S8("selection") : reason);
                }
            }
            record_valid &= source.length && !string_equal(source, S8("-")) &&
                name.length && !string_equal(name, S8("-"));
            // A census row compiles one TU, so function IDs strictly increase.
            // They need not be dense: declarations need not have lowered bodies.
            record_valid &= reason_found && (records == 0 || function > previous_function);
            valid &= record_valid;
            previous_function = function;
            records += 1;
            if (settings->functions)
            {
                fprintf(settings->functions, "%llu\t%u\t%.*s row=%llu\n", (unsigned long long)row,
                        (unsigned)record_valid, (int)line.length, line.pointer, (unsigned long long)row);
            }
        }
    }
    return valid && summaries == 1 && declared == records && records == expected;
}

BUSTER_GLOBAL_LOCAL String8 nrc_target_features(Arena* arena, NrcTarget target, String8 cpu)
{
    TargetParseResult parsed = target_parse_triple(target.triple);
    CpuModel model = cpu_model_from_string(cpu);
    String8 result = {0};
    if (parsed.error == TARGET_PARSE_ERROR_NONE && model != CPU_MODEL_ERROR && cpu_model_supports_arch(model, parsed.target.cpu_arch))
    {
        parsed.target.cpu_model = model;
        parsed.target.cpu_features_explicit = false;
        parsed.target.cpu_features = target_cpu_features_empty();
        result = target_cpu_features_to_string(arena, parsed.target);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 nrc_linux_musl_arch(u32 target)
{
    String8 result = {0};
    if (target == 0) { result = S8("x86_64"); }
    else if (target == 1) { result = S8("aarch64"); }
    return result;
}

BUSTER_GLOBAL_LOCAL void nrc_write_argv(NrcSettings* settings, String8 prefix, String8* command, u64 command_count)
{
    Arena* arena = settings->child.arena;
    u64 bytes = command_count;
    for (u64 index = 0; index < command_count; index += 1) { bytes += command[index].length; }
    u8* serialized = arena_allocate(arena, u8, bytes);
    u64 at = 0;
    for (u64 index = 0; index < command_count; index += 1)
    {
        memcpy(serialized + at, command[index].pointer, command[index].length);
        at += command[index].length;
        serialized[at++] = 0;
    }
    String8 path = string_format(arena, S8("{S8}.argv"), prefix);
    d_write(&settings->child, path, (String8){.pointer = (char8*)serialized, .length = bytes});
}

BUSTER_GLOBAL_LOCAL void nrc_group(NrcSettings* settings, NrcInput input, u32 target, u32 frontend, u32 pic, u64 group)
{
    Arena* arena = settings->child.arena;
    String8 directory = string_format_z(arena, S8("{S8}/groups/{u64}"), settings->child.out, group);
    make_directory_recursive(arena, directory);
    NrcStatistics baseline = {0};
    bool baseline_supported = false;
    for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(nrc_allocators); mode += 1)
    {
        TemporalArena temporary = scratch_begin(&arena, 1);
        DSettings child = settings->child;
        child.arena = temporary.arena;
        String8 prefix = path_join(temporary.arena, directory, nrc_allocators[mode]);
        String8 object = string_format_z(temporary.arena, S8("{S8}.o"), prefix);
        String8 source = path_join(temporary.arena, settings->snapshot, input.path);
        String8 allocator = string_format(temporary.arena, S8("-fregister-allocator={S8}"), nrc_allocators[mode]);
        String8 include = string_format(temporary.arena, S8("-I{S8}/tests"), settings->snapshot);
        NrcFixtureRecipe recipe = nrc_fixture_recipe(input.path);
        String8 recipe_cpu = nrc_recipe_cpu(recipe, nrc_targets[target], settings->cpu);
        String8 cpu = string_format(temporary.arena, S8("-mcpu={S8}"), recipe_cpu);
        String8 command[48] = {0};
        u64 command_count = 0;
        command[command_count++] = mode ? settings->child.ide : settings->baseline;
        command[command_count++] = S8("cc");
        command[command_count++] = S8("-c");
        command[command_count++] = S8("-g0");
        command[command_count++] = S8("-v");
        command[command_count++] = S8("-fwrapv");
        command[command_count++] = S8("-fno-strict-aliasing");
        command[command_count++] = S8("-funsigned-char");
        command[command_count++] = S8("-target");
        command[command_count++] = nrc_targets[target].triple;
        command[command_count++] = cpu;
        command[command_count++] = pic ? S8("-fPIC") : S8("-fno-pic");
        command[command_count++] = frontend ? S8("-ffrontend-ssa") : S8("-fno-frontend-ssa");
        command[command_count++] = allocator;
        command[command_count++] = S8("-fverify-codegen");
        command[command_count++] = mode ? S8("-fno-machine-fallback") : S8("-fmachine-fallback");
        command[command_count++] = S8("-nostdinc");
        command[command_count++] = S8("-isystem");
        command[command_count++] = settings->resource_snapshot;
        String8 musl_arch = nrc_linux_musl_arch(target);
        if (musl_arch.length && settings->project_snapshot.length)
        {
            command[command_count++] = S8("-isystem");
            command[command_count++] = string_format(temporary.arena, S8("{S8}/musl/{S8}/include"),
                                                     settings->project_snapshot, musl_arch);
            command[command_count++] = S8("-isystem");
            command[command_count++] = string_format(temporary.arena, S8("{S8}/musl/include"), settings->project_snapshot);
        }
        if (nrc_hosted_fixture(input.path) && settings->project_snapshot.length && target >= 2 && target < 10)
        {
            String8 sdk = S8("darwin");
            if (target == 2 || target == 3)
            {
                sdk = S8("windows");
                command[command_count++] = S8("-U__GNUC__");
                if (target == 2) { command[command_count++] = S8("-D__x86_64=1"); }
                command[command_count++] = S8("-isystem");
                command[command_count++] = string_format(temporary.arena, S8("{S8}/sdk/mingw-adapter"), settings->project_snapshot);
            }
            else if (target == 6 || target == 7)
            {
                sdk = S8("android");
                command[command_count++] = S8("-isystem");
                command[command_count++] = string_format(temporary.arena, S8("{S8}/sdk/android/{S8}-linux-android"),
                    settings->project_snapshot, target == 6 ? S8("x86_64") : S8("aarch64"));
            }
            command[command_count++] = S8("-isystem");
            command[command_count++] = string_format(temporary.arena, S8("{S8}/sdk/{S8}"), settings->project_snapshot, sdk);
        }
        command[command_count++] = include;
        if (settings->project_snapshot.length)
        {
            String8 project_include = string_format(temporary.arena, S8("-I{S8}"), settings->project_snapshot);
            command[command_count++] = project_include;
        }
        command[command_count++] = source;
        command[command_count++] = S8("-o");
        command[command_count++] = object;
        for (u32 flag = 0; flag < recipe.count; flag += 1) { command[command_count++] = recipe.flags[flag]; }
        if (mode) { command[command_count++] = S8("-fcodegen-fallback-census"); }
        SliceString8 argv = {.pointer = command, .length = command_count};
        NrcApplicability* applicability = nrc_applicability_lookup(settings, input.path, nrc_targets[target].triple);
        bool non_object_control = string_equal(input.compile_obligation, S8("registered-non-object-control"));
        bool authenticated_skip = applicability &&
            (string_equal(applicability->applicability, S8("platform-inapplicable")) ||
             string_equal(applicability->applicability, S8("unavailable")));
        if (non_object_control || authenticated_skip)
        {
            String8 disposition = non_object_control ? S8("retained-control") : applicability->applicability;
            String8 reason = non_object_control ? S8("registered-non-object-control") : applicability->reason;
            u64 row = group * BUSTER_ARRAY_LENGTH(nrc_allocators) + mode;
            String8 expected_features = nrc_target_features(temporary.arena, nrc_targets[target], recipe_cpu);
            nrc_write_argv(settings, prefix, command, command_count);
            d_write(&settings->child, string_format(temporary.arena, S8("{S8}.stdout"), prefix), S8(""));
            d_write(&settings->child, string_format(temporary.arena, S8("{S8}.stderr"), prefix), S8(""));
            fprintf(settings->rows, "%llu\t%llu\t%.*s\t0\t0\t1\t1\t1\t0\t0\t0\t%.*s\t%.*s\t0\t0\t\n",
                    (unsigned long long)row, (unsigned long long)group,
                    (int)disposition.length, disposition.pointer,
                    (int)recipe_cpu.length, recipe_cpu.pointer,
                    (int)expected_features.length, expected_features.pointer);
            fprintf(settings->skips, "%llu\t%llu\t%.*s\t%.*s\t%.*s\t%.*s\t%.*s\n",
                    (unsigned long long)row, (unsigned long long)group,
                    (int)input.path.length, input.path.pointer,
                    (int)nrc_targets[target].triple.length, nrc_targets[target].triple.pointer,
                    (int)nrc_allocators[mode].length, nrc_allocators[mode].pointer,
                    (int)disposition.length, disposition.pointer,
                    (int)reason.length, reason.pointer);
            settings->skipped_rows += 1;
            settings->child.io_failed |= child.io_failed || fflush(settings->rows) != 0 || fflush(settings->skips) != 0;
            scratch_end(temporary);
            continue;
        }
        DObservation observed = d_observe(&child, argv, prefix);
        String8 provenance_record = string_format_z(temporary.arena, S8("{S8}.provenance"), prefix);
        String8 tree_identity = mode ? settings->compiler_revision : settings->baseline_revision;
        if (!tree_identity.length) { tree_identity = stage_object_source_tree_identity(temporary.arena); }
        StageObjectProvenance provenance = {
            .object_path = object,
            .source_path = source,
            .compiler_path = command[0],
            .record_path = provenance_record,
            .tree_identity = tree_identity,
            // These copies are SHA-256 authenticated before any census row executes.
            .authenticated_source_sha256 = input.sha256,
            .authenticated_compiler_sha256 = mode ? settings->compiler_sha256 : settings->baseline_sha256,
            .toolchain_arguments = argv,
        };
        bool provenance_valid = d_success(observed) &&
                                stage_object_provenance_capture(temporary.arena, &provenance, true) &&
                                stage_object_provenance_validate(temporary.arena, &provenance, true);
        NrcStatistics statistics = nrc_statistics(observed.output, nrc_allocators[mode]);
        u64 row = group * BUSTER_ARRAY_LENGTH(nrc_allocators) + mode;
        String8 expected_features = nrc_target_features(temporary.arena, nrc_targets[target], recipe_cpu);
        bool target_valid = expected_features.length && string_equal(statistics.cpu, recipe_cpu) &&
                            string_equal(statistics.features, expected_features);
        bool records_valid = !mode || nrc_function_records(settings, row, observed.output, nrc_allocators[mode], statistics.fallbacks);
        TargetParseResult target_parse = target_parse_triple(nrc_targets[target].triple);
        String8 diagnostic_target = string_format(temporary.arena, S8("{S8}-{S8}"),
                                                  cpu_arch_to_string_os(target_parse.target.cpu_arch),
                                                  operating_system_to_string_os(target_parse.target.os));
        nrc_counters(settings, row, observed.output, diagnostic_target, nrc_allocators[mode]);
        u64 object_hash = 0, object_bytes = 0;
        String8 object_sha256 = {0};
        bool artifact = provenance_valid && build_artifact_fanout_hash_file(temporary.arena, object, &object_hash, &object_bytes);
        if (artifact)
        {
            u64 identity_bytes = 0, identity_hash = 0;
            artifact = nrc_file_identity(temporary.arena, object, &identity_bytes, &identity_hash, &object_sha256) &&
                       identity_bytes == object_bytes && identity_hash == object_hash;
        }
        bool success = d_success(observed) && artifact && statistics.valid && target_valid && records_valid && statistics.fallbacks == 0 &&
                       d_verification(&child, &observed, (DConfig){.allocator = mode});
        String8 disposition;
        if (!mode)
        {
            baseline = statistics;
            baseline_supported = success;
            disposition = success ? S8("baseline-supported") : S8("baseline-unresolved");
            settings->baseline_failures += !success;
        }
        else if (success)
        {
            if (baseline_supported && statistics.functions != baseline.functions)
            {
                disposition = S8("function-count-mismatch");
                settings->failures += 1;
            }
            else
            {
                disposition = baseline_supported ? (statistics.functions ? S8("strict-success") : S8("strict-empty-unit"))
                                                 : S8("strict-success-baseline-unresolved");
                settings->strict_successes += statistics.functions != 0;
                settings->strict_empty += statistics.functions == 0;
            }
        }
        else if (!d_normal(observed) || d_success(observed) || artifact)
        {
            disposition = S8("infrastructure-or-protocol-failure");
            settings->failures += 1;
        }
        else
        {
            disposition = baseline_supported ? (statistics.valid && records_valid ? S8("supported-native-gap") : S8("supported-native-gap-missing-telemetry"))
                                             : S8("baseline-and-mir-unresolved");
            settings->gaps += baseline_supported;
            // An unresolved reference does not excuse a failing MIR child.
            // Keep the reference counter separate, as the validator does.
            settings->failures += !baseline_supported || !statistics.valid || !records_valid;
        }
        fprintf(settings->rows, "%llu\t%llu\t%.*s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%.*s\t%.*s\t%llu\t%llu\t%.*s\n",
            (unsigned long long)row, (unsigned long long)group,
            (int)disposition.length, disposition.pointer, (unsigned)observed.kind, observed.status,
            (unsigned)statistics.valid, (unsigned)target_valid, (unsigned)records_valid, statistics.functions, statistics.fallbacks,
            baseline.functions, (int)statistics.cpu.length, statistics.cpu.pointer, (int)statistics.features.length, statistics.features.pointer,
            (unsigned long long)object_bytes, (unsigned long long)object_hash,
            (int)object_sha256.length, object_sha256.pointer);
        settings->child.io_failed |= child.io_failed || fflush(settings->rows) != 0 || fflush(settings->counters) != 0 || fflush(settings->functions) != 0;
        scratch_end(temporary);
    }
    settings->selected_groups += 1;
}

BUSTER_GLOBAL_LOCAL u64 nrc_manifest(NrcSettings* settings, NrcInput* inputs, u64 count, bool run)
{
    FILE* manifest = run ? 0 : nrc_open(settings, S8("rows.tsv"));
    if (manifest)
    {
        fprintf(manifest, "row\tgroup\tfixture\ttarget\ttarget_abi\tcpu\tcpu_features\tallocator\tfrontend_lowering\tPIC\tselected\t"
                          "fixture_recipe\tcompile_obligation\tlink_obligation\texecution_obligation\tdiagnostic_obligation\targv_evidence\n");
    }
    u64 group = 0;
    for (u64 input = 0; input < count; input += 1)
    {
        if (string_equal(inputs[input].role, S8("subject")))
        {
            NrcFixtureRecipe recipe = nrc_fixture_recipe(inputs[input].path);
            for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(nrc_targets); target += 1)
            {
                for (u32 frontend = 0; frontend < 2; frontend += 1)
                {
                    for (u32 pic = 0; pic < 2; pic += 1)
                    {
                        bool selected = nrc_selected(settings, inputs[input].path, nrc_targets[target].triple, group);
                        if (run && selected && !settings->child.io_failed)
                        {
                            nrc_group(settings, inputs[input], target, frontend, pic, group);
                        }
                        if (manifest)
                        {
                            for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(nrc_allocators); mode += 1)
                            {
                                String8 recipe_cpu = nrc_recipe_cpu(recipe, nrc_targets[target], settings->cpu);
                                String8 features = nrc_target_features(settings->child.arena, nrc_targets[target], recipe_cpu);
                                String8 lowering = frontend ? S8("direct-ssa") : S8("local-backed-canonical");
                                String8 diagnostic = string_equal(inputs[input].role, S8("negative-diagnostic-fixture"))
                                                         ? S8("registered-rejection-control") : S8("none");
                                String8 argv_evidence = string_format(settings->child.arena, S8("groups/{u64}/{S8}.argv"), group,
                                                                      nrc_allocators[mode]);
                                fprintf(manifest,
                                    "%llu\t%llu\t%.*s\t%.*s\t%.*s\t%.*s\t%.*s\t%.*s\t%.*s\t%u\t%u\t%.*s\t%.*s\t%.*s\t%.*s\t%.*s\t%.*s\n",
                                    (unsigned long long)(group * BUSTER_ARRAY_LENGTH(nrc_allocators) + mode), (unsigned long long)group,
                                    (int)inputs[input].path.length, inputs[input].path.pointer, (int)nrc_targets[target].triple.length,
                                    nrc_targets[target].triple.pointer, (int)nrc_targets[target].abi.length, nrc_targets[target].abi.pointer,
                                    (int)recipe_cpu.length, recipe_cpu.pointer, (int)features.length, features.pointer,
                                    (int)nrc_allocators[mode].length, nrc_allocators[mode].pointer, (int)lowering.length, lowering.pointer,
                                    pic, (unsigned)selected, (int)recipe.name.length, recipe.name.pointer,
                                    (int)inputs[input].compile_obligation.length, inputs[input].compile_obligation.pointer,
                                    (int)nrc_targets[target].link_obligation.length, nrc_targets[target].link_obligation.pointer,
                                    (int)nrc_targets[target].execution_obligation.length, nrc_targets[target].execution_obligation.pointer,
                                    (int)diagnostic.length, diagnostic.pointer, (int)argv_evidence.length, argv_evidence.pointer);
                            }
                        }
                        group += 1;
                    }
                }
            }
        }
    }
    nrc_close(settings, manifest);
    return group;
}

BUSTER_GLOBAL_LOCAL u32 nrc_self_test(Arena* arena)
{
    u32 failures = 0;
    failures += !string_equal(nrc_role(S8("tests/basic_c_negative_constant_widening.c")), S8("subject"));
    failures += !string_equal(nrc_role(S8("tests/basic_c_invalid_labels.c")), S8("negative-diagnostic-fixture"));
    String8 rejection_contract[] = {S8("tests/basic_c_bit_field_alignas.c"), S8("tests/basic_c_function_pointer_conflict.c"),
        S8("tests/basic_c_asm_literal_register.c"), S8("tests/basic_c_sizeof_missing_member.c"), S8("tests/basic_c_sizeof_parenthesized_type.c")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rejection_contract); index += 1)
    {
        failures += !string_equal(nrc_role(rejection_contract[index]), S8("negative-diagnostic-fixture"));
    }
    failures += !string_equal(nrc_role(S8("tests/differential/basic_c_bit_field_alignas.c")), S8("subject"));
    failures += !string_equal(nrc_role(S8("tests/basic_c_bit_field_aligned.c")), S8("subject"));
    failures += !string_equal(nrc_role(S8("tests/basic_if_else.bbb")), S8("dormant-custom-language"));
    failures += !string_equal(nrc_role(S8("tests/basic_c_guarded_include.h")), S8("support-file"));
    String8 c23_contract[] = {S8("tests/basic_c_constexpr.c"), S8("tests/basic_c_constexpr_leaf.c"),
                             S8("tests/basic_c_typeof.c"), S8("tests/basic_c_nullptr.c")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c23_contract); index += 1)
    {
        NrcFixtureRecipe recipe = nrc_fixture_recipe(c23_contract[index]);
        failures += recipe.count != 1 || !string_equal(recipe.flags[0], S8("-std=c23"));
    }
    NrcFixtureRecipe dialect = nrc_fixture_recipe(S8("tests/basic_c_dialect.c"));
    failures += dialect.count != 3 || !string_equal(dialect.flags[0], S8("-std=c23")) ||
        !string_equal(dialect.flags[1], S8("-DEXPECTED_STDC_VERSION=202311L")) || !string_equal(dialect.flags[2], S8("-DEXPECTED_GNU=0"));
    failures += nrc_fixture_recipe(S8("tests/basic_c_typeof_declaration.c")).count != 0;
    failures += nrc_fixture_recipe(S8("tests/differential/basic_c_constexpr.c")).count != 0;
    NrcFixtureRecipe predicate = nrc_fixture_recipe(S8("tests/basic_c_predicate_bank.c"));
    failures += !string_equal(predicate.name, S8("x86-avx512")) ||
                !string_equal(nrc_recipe_cpu(predicate, nrc_targets[0], S8("baseline")), S8("skylake-avx512")) ||
                !string_equal(nrc_recipe_cpu(predicate, nrc_targets[1], S8("baseline")), S8("baseline"));
    NrcFixtureRecipe atomic = nrc_fixture_recipe(S8("tests/basic_c_atomic_aggregate.c"));
    failures += !string_equal(atomic.name, S8("x86-cx16")) ||
                !string_equal(nrc_recipe_cpu(atomic, nrc_targets[0], S8("baseline")), S8("haswell")) ||
                !string_equal(nrc_recipe_cpu(atomic, nrc_targets[1], S8("baseline")), S8("baseline"));
    failures += nrc_field_safe(S8("bad\tpath")) || nrc_revision_valid(S8("main"));
    failures += !nrc_revision_valid(S8("641cd88d33decfd56fa2da9a960c6ac075935a71"));
    String8 applicability_text = S8("tests/a.c\tx86_64-unknown-linux-gnu\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\tadmitted-supported\tsource-reviewed-residual\n");
    NrcApplicability parsed_applicability = {0};
    failures += !nrc_applicability_row(&applicability_text, &parsed_applicability) || applicability_text.length != 0;
    NrcApplicability applicability_records[] = {parsed_applicability};
    NrcSettings applicability_settings = {.applicability = applicability_records, .applicability_count = 1};
    failures += nrc_applicability_lookup(&applicability_settings, S8("tests/missing.c"), nrc_targets[0].triple) != 0;
    failures += nrc_applicability_lookup(&applicability_settings, S8("tests/a.c"), nrc_targets[0].triple) != applicability_records;
    failures += !string_equal(nrc_linux_musl_arch(0), S8("x86_64"));
    failures += !string_equal(nrc_linux_musl_arch(1), S8("aarch64"));
    failures += nrc_linux_musl_arch(2).length != 0 || nrc_linux_musl_arch(6).length != 0;
    failures += !string_equal(nrc_compile_obligation(S8("tests/basic_c_operations.c"), S8("subject")), S8("supported-object-zero-fallback"));
    failures += !string_equal(nrc_compile_obligation(S8("tests/basic_c_macro_options.c"), S8("subject")), S8("registered-non-object-control"));
    failures += !string_equal(nrc_compile_obligation(S8("tests/ebpf_scalar_regression.c"), S8("subject")), S8("registered-non-object-control"));
    failures += !string_equal(nrc_compile_obligation(S8("tests/basic_c_sizeof_anonymous_aggregate.c"), S8("subject")), S8("supported-object-zero-fallback"));
    failures += !string_equal(nrc_compile_obligation(S8("tests/basic_c_invalid_labels.c"), S8("negative-diagnostic-fixture")), S8("registered-rejection-control"));
    NrcStatistics valid = nrc_statistics(S8("TARGET cpu=baseline features=sse,sse2\n"
                                             "CODEGEN functions=4 allocator=fast fallback_functions=2\n"), S8("fast"));
    failures += !valid.valid || valid.functions != 4 || valid.fallbacks != 2 || !string_equal(valid.cpu, S8("baseline")) ||
                !string_equal(valid.features, S8("sse,sse2"));
    String8 invalid[] = {S8(""), S8("CODEGEN functions=4 allocator=none fallback_functions=0\n"),
        S8("TARGET cpu=baseline features=sse,sse2\nCODEGEN functions=4 allocator=fast fallback_functions=5\n"),
        S8("TARGET cpu=baseline features=sse,sse2\nCODEGEN functions=-1 allocator=fast fallback_functions=0\n"),
        S8("TARGET cpu=baseline features=sse,sse2\nCODEGEN functions=4294967296 allocator=fast fallback_functions=0\n"),
        S8("TARGET cpu=baseline features=sse,sse2\nCODEGEN functions=4 functions=4 allocator=fast fallback_functions=0\n"),
        S8("TARGET cpu=baseline features=sse,sse2\nCODEGEN functions=4 allocator=fast fallback_functions=0\n"
           "CODEGEN functions=4 allocator=fast fallback_functions=0\n"),
        S8("TARGET cpu=baseline features=sse,sse2\nTARGET cpu=baseline features=sse,sse2\n"
           "CODEGEN functions=4 allocator=fast fallback_functions=0\n")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1) { failures += nrc_statistics(invalid[index], S8("fast")).valid; }
    NrcSettings records_settings = {0};
    String8 first_record = S8("CODEGEN_FALLBACK_FUNCTION version=1 target=x86_64-linux allocator=fast function_id=7 reason=signature stage=selection opcode_id=4294967295 line=3 column=1 source_hex=612063 function_hex=66\n");
    String8 second_record = S8("CODEGEN_FALLBACK_FUNCTION version=1 target=x86_64-linux allocator=fast function_id=9 reason=encoding stage=encoding opcode_id=4294967295 line=9 column=2 source_hex=612063 function_hex=67\n");
    String8 record_text = string_format(arena, S8("CODEGEN_FALLBACK_CENSUS version=1 records=2\n{S8}{S8}"), first_record, second_record);
    failures += !nrc_function_records(&records_settings, 0, record_text, S8("fast"), 2);
    failures += nrc_function_records(&records_settings, 0, record_text, S8("fast"), 1);
    failures += nrc_function_records(&records_settings, 0, record_text, S8("none"), 2);
    failures += nrc_function_records(&records_settings, 0, first_record, S8("fast"), 1);
    failures += nrc_function_records(&records_settings, 0,
        string_format(arena, S8("CODEGEN_FALLBACK_CENSUS version=1 records=2\n{S8}{S8}"), first_record, first_record), S8("fast"), 2);
    failures += nrc_function_records(&records_settings, 0,
        string_format(arena, S8("CODEGEN_FALLBACK_CENSUS version=2 records=2\n{S8}{S8}"), first_record, second_record), S8("fast"), 2);
    failures += !nrc_function_records(&records_settings, 0, S8("CODEGEN_FALLBACK_CENSUS version=1 records=0\n"), S8("fast"), 0);
    String8 blank_source_record = S8("CODEGEN_FALLBACK_CENSUS version=1 records=1\n"
                                     "CODEGEN_FALLBACK_FUNCTION version=1 target=x86_64-linux allocator=fast function_id=7 "
                                     "reason=signature stage=selection opcode_id=4294967295 line=3 column=1 source_hex=- function_hex=66\n");
    String8 blank_function_record = S8("CODEGEN_FALLBACK_CENSUS version=1 records=1\n"
                                       "CODEGEN_FALLBACK_FUNCTION version=1 target=x86_64-linux allocator=fast function_id=7 "
                                       "reason=signature stage=selection opcode_id=4294967295 line=3 column=1 source_hex=612063 function_hex=-\n");
    failures += nrc_function_records(&records_settings, 0, blank_source_record, S8("fast"), 1);
    failures += nrc_function_records(&records_settings, 0, blank_function_record, S8("fast"), 1);
    failures += nrc_hex_field(S8("a")) || nrc_hex_field(S8("0g")) || !nrc_hex_field(S8("-"));
    for (u64 group = 0; group < 101; group += 1)
    {
        u32 selected = 0;
        for (u32 shard = 0; shard < 7; shard += 1)
        {
            NrcSettings settings = {.shard_index = shard, .shard_count = 7};
            selected += nrc_selected(&settings, S8("tests/basic_c_operations.c"), nrc_targets[0].triple, group);
        }
        failures += selected != 1;
    }
    String8 contract = S8("tests/a.c\tsubject\tsupported-object-zero-fallback\t7\t"
                          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
    NrcInput approved = {0};
    failures += !nrc_contract_row(&contract, &approved) || approved.bytes != 7 || !string_equal(approved.path, S8("tests/a.c")) ||
                !string_equal(approved.role, S8("subject")) || contract.length != 0;
    failures += nrc_contract_row(&contract, &approved);
    for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(nrc_targets); target += 1)
    {
        failures += !nrc_target_features(arena, nrc_targets[target], S8("baseline")).length;
    }
    string_print(S8("NATIVE_RETIREMENT_CENSUS_SELF_TEST failures={u32}\n"), failures);
    return failures;
}

BUSTER_GLOBAL_LOCAL ProcessResult native_retirement_census_main(Arena* arena, SliceString8 arguments)
{
    NrcSettings settings = {.child = {.arena = arena, .ide = S8("build/Release/ide"),
        .out = S8("build/native-retirement-census"), .timeout_seconds = 30, .verify = true},
        .shard_count = 1, .cpu = S8("baseline"), .contract_path = S8("docs/native-retirement-support-v1.tsv"),
        .supported_gap_ledger_path = S8("docs/native-retirement-supported-gaps-v1.tsv"),
        .applicability_ledger_path = S8("docs/native-retirement-applicability-v1.tsv"),
        .dependency_manifest = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_POLICY_PATH),
        .dependency_snapshot = S8_INITIALIZER(BUSTER_NATIVE_RETIREMENT_SNAPSHOT_PATH)};
    bool valid = true, self_test = false;
    for (u64 index = 0; valid && index < arguments.length; index += 1)
    {
        String8 option = arguments.pointer[index];
        if (string_equal(option, S8("--manifest-only"))) { settings.manifest_only = true; }
        else if (string_equal(option, S8("--self-test"))) { self_test = true; }
        else if (index + 1 == arguments.length) { valid = false; }
        else
        {
            String8 value = arguments.pointer[++index];
            valid &= nrc_field_safe(value);
            if (string_equal(option, S8("--ide"))) { settings.child.ide = value; }
            else if (string_equal(option, S8("--baseline-ide"))) { settings.baseline = value; }
            else if (string_equal(option, S8("--compiler-revision"))) { settings.compiler_revision = value; }
            else if (string_equal(option, S8("--baseline-revision"))) { settings.baseline_revision = value; }
            else if (string_equal(option, S8("--out"))) { settings.child.out = value; }
            else if (string_equal(option, S8("--fixture"))) { settings.fixture_filter = value; }
            else if (string_equal(option, S8("--target"))) { settings.target_filter = value; }
            else if (string_equal(option, S8("--cpu"))) { settings.cpu = value; }
            else if (string_equal(option, S8("--resource-include"))) { settings.resource_include = value; }
            else if (string_equal(option, S8("--project-include"))) { settings.project_include = value; }
            else if (string_equal(option, S8("--dependency-manifest"))) { settings.dependency_manifest = value; }
            else if (string_equal(option, S8("--dependency-snapshot"))) { settings.dependency_snapshot = value; }
            else if (string_equal(option, S8("--dependency-receipt"))) { settings.dependency_receipt = value; }
            else if (string_equal(option, S8("--shard-index"))) { valid &= d_number(value, &settings.shard_index); }
            else if (string_equal(option, S8("--shard-count"))) { valid &= d_number(value, &settings.shard_count) && settings.shard_count > 0; }
            else if (string_equal(option, S8("--timeout")))
            {
                valid &= d_number(value, &settings.child.timeout_seconds) && settings.child.timeout_seconds > 0 && settings.child.timeout_seconds <= 3600;
            }
            else { valid = false; }
        }
    }
    valid &= settings.shard_index < settings.shard_count;
    if (!settings.baseline.length)
    {
        settings.baseline = settings.child.ide;
        if (!settings.baseline_revision.length) { settings.baseline_revision = settings.compiler_revision; }
    }
    valid &= self_test || settings.manifest_only || (nrc_revision_valid(settings.compiler_revision) && nrc_revision_valid(settings.baseline_revision));
    bool target_found = !settings.target_filter.length;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(nrc_targets); index += 1)
    {
        target_found |= string_equal(settings.target_filter, nrc_targets[index].triple);
        if (!settings.target_filter.length || string_equal(settings.target_filter, nrc_targets[index].triple))
        {
            valid &= nrc_target_features(arena, nrc_targets[index], settings.cpu).length != 0;
        }
    }
    valid &= target_found && (self_test || settings.manifest_only || settings.resource_include.length != 0);
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    if (!valid)
    {
        string_print(S8("usage: native_retirement_census --compiler-revision <40-hex> [--ide path] [--baseline-ide path --baseline-revision <40-hex>] "
                        "[--out new-directory] [--fixture substring] [--target triple] [--cpu model] [--resource-include directory] "
                        "[--project-include directory] [--dependency-manifest path --dependency-snapshot path --dependency-receipt path] "
                        "[--shard-index N --shard-count N] "
                        "[--timeout seconds] [--manifest-only] [--self-test]\n"));
        result = PROCESS_RESULT_FAILED;
    }
    else if (self_test) { result = nrc_self_test(arena) ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS; }
    else if (!d_create_output(arena, settings.child.out))
    {
        string_print(S8("error: census requires a new output directory\n"));
        result = PROCESS_RESULT_FAILED;
    }
    else
    {
        settings.child.out = os_path_absolute(arena, settings.child.out, true);
        settings.snapshot = path_join(arena, settings.child.out, S8("inputs"));
        settings.resource_snapshot = path_join(arena, settings.child.out, S8("dependencies/resource-include"));
        settings.dependency_manifest = os_path_absolute(arena, settings.dependency_manifest, true);
        settings.dependency_snapshot = os_path_absolute(arena, settings.dependency_snapshot, true);
        if (settings.project_include.length)
        {
            settings.project_snapshot = path_join(arena, settings.child.out, S8("dependencies/project-include"));
        }
        if (settings.dependency_receipt.length)
        {
            settings.dependency_receipt = os_path_absolute(arena, settings.dependency_receipt, true);
        }
        settings.child.report = nrc_open(&settings, S8("processes.tsv"));
        if (settings.child.report)
        {
            fprintf(settings.child.report, "prefix\tkind_0exit_1signal_2timeout_3spawn_4wait\tstatus\traw_status\tsanitizer\telapsed_us\n");
        }
        String8 git = executable_resolve_in_path(arena, S8("git"));
        String8 head_command[] = {git, S8("rev-parse"), S8("HEAD")};
        String8 status_command[] = {git, S8("status"), S8("--porcelain"), S8("--untracked-files=all")};
        DObservation head = d_observe(&settings.child, (SliceString8)BUSTER_ARRAY_TO_SLICE(head_command), path_join(arena, settings.child.out, S8("fixture-revision")));
        DObservation status = d_observe(&settings.child, (SliceString8)BUSTER_ARRAY_TO_SLICE(status_command), path_join(arena, settings.child.out, S8("worktree-status")));
        settings.child.io_failed |= !d_success(head) || !d_success(status);
        u64 input_count = 0;
        NrcInput* inputs = nrc_inventory(&settings, &input_count);
        u64 groups = nrc_manifest(&settings, inputs, input_count, false);
        u64 subject_count = 0;
        for (u64 index = 0; index < input_count; index += 1)
        {
            subject_count += string_equal(inputs[index].role, S8("subject"));
        }
        bool full_profile = !settings.fixture_filter.length && !settings.target_filter.length && settings.shard_count == 4 &&
                            input_count == nrc_full_input_count && subject_count == nrc_full_subject_count && groups == nrc_full_group_count &&
                            groups * BUSTER_ARRAY_LENGTH(nrc_allocators) == nrc_full_row_count;
        bool dependency_required = !settings.manifest_only && (full_profile || settings.project_include.length != 0);
        if (dependency_required && (!settings.project_include.length || !settings.dependency_receipt.length))
        {
            settings.child.io_failed = true;
        }
        String8 profile = full_profile ? S8("full-census") : S8("self-test");
        u64 compiler_hash = 0, compiler_bytes = 0, baseline_hash = 0, baseline_bytes = 0;
        String8 compiler_sha256 = {0}, baseline_sha256 = {0};
        if (!settings.manifest_only && !settings.child.io_failed)
        {
            settings.child.ide = os_path_absolute(arena, settings.child.ide, true);
            settings.baseline = os_path_absolute(arena, settings.baseline, true);
            settings.resource_include = os_path_absolute(arena, settings.resource_include, true);
            if (settings.project_include.length) { settings.project_include = os_path_absolute(arena, settings.project_include, true); }
            bool dependency_valid = nrc_file_identity(arena, settings.child.ide, &compiler_bytes, &compiler_hash, &compiler_sha256) &&
                                    nrc_file_identity(arena, settings.baseline, &baseline_bytes, &baseline_hash, &baseline_sha256) &&
                                    nrc_snapshot_resource(&settings);
            dependency_valid &= nrc_dependency_binding(&settings, dependency_required);
            settings.child.io_failed |= !dependency_valid;
            // The existing fanout helper preserves executable permissions and
            // verifies both bytes and fingerprint before admitting the copy.
            String8 candidate_copy = path_join(arena, settings.child.out, S8("candidate-ide.exe"));
            String8 baseline_copy = path_join(arena, settings.child.out, S8("baseline-ide.exe"));
            if (!settings.child.io_failed)
            {
                bool snapshots_valid = build_artifact_fanout_snapshot(arena, settings.child.ide, candidate_copy,
                                                                       compiler_hash, compiler_bytes) &&
                                       build_artifact_fanout_snapshot(arena, settings.baseline, baseline_copy,
                                                                       baseline_hash, baseline_bytes);
                if (snapshots_valid)
                {
                    u64 candidate_bytes = 0, candidate_hash = 0;
                    u64 copied_baseline_bytes = 0, copied_baseline_hash = 0;
                    String8 candidate_sha256 = {0}, copied_baseline_sha256 = {0};
                    snapshots_valid = nrc_file_identity(arena, candidate_copy, &candidate_bytes, &candidate_hash,
                                                        &candidate_sha256) &&
                                      nrc_file_identity(arena, baseline_copy, &copied_baseline_bytes,
                                                        &copied_baseline_hash, &copied_baseline_sha256) &&
                                      candidate_bytes == compiler_bytes && candidate_hash == compiler_hash &&
                                      string_equal(candidate_sha256, compiler_sha256) &&
                                      copied_baseline_bytes == baseline_bytes && copied_baseline_hash == baseline_hash &&
                                      string_equal(copied_baseline_sha256, baseline_sha256);
                }
                settings.child.io_failed |= !snapshots_valid;
                if (snapshots_valid)
                {
                    settings.child.ide = candidate_copy;
                    settings.baseline = baseline_copy;
                    settings.compiler_sha256 = compiler_sha256;
                    settings.baseline_sha256 = baseline_sha256;
                }
            }
            nrc_explicit_environment(&settings);
        }
        ByteSlice contract = file_read(arena, settings.contract_path, (FileReadOptions){0});
        if (contract.pointer)
        {
            d_write(&settings.child, path_join(arena, settings.child.out, S8("support-contract.tsv")), BYTE_SLICE_TO_STRING(8, contract));
        }
        else { settings.child.io_failed = true; }
        ByteSlice supported_gap_ledger = file_read(arena, settings.supported_gap_ledger_path, (FileReadOptions){0});
        if (supported_gap_ledger.pointer)
        {
            settings.supported_gap_ledger_sha256 = nrc_sha256(arena, supported_gap_ledger.pointer, supported_gap_ledger.length);
            d_write(&settings.child, path_join(arena, settings.child.out, S8("supported-gap-ledger.tsv")),
                    BYTE_SLICE_TO_STRING(8, supported_gap_ledger));
        }
        else { settings.child.io_failed = true; }
        ByteSlice applicability_ledger = file_read(arena, settings.applicability_ledger_path, (FileReadOptions){0});
        if (applicability_ledger.pointer)
        {
            settings.applicability_ledger_sha256 = nrc_sha256(arena, applicability_ledger.pointer, applicability_ledger.length);
            d_write(&settings.child, path_join(arena, settings.child.out, S8("applicability-ledger.tsv")),
                    BYTE_SLICE_TO_STRING(8, applicability_ledger));
            settings.child.io_failed |= !nrc_load_applicability(&settings, applicability_ledger, inputs, input_count);
        }
        else { settings.child.io_failed = true; }
        String8 dependency_state = settings.manifest_only ? S8("not-executed-manifest-only") : S8("none-for-object-census");
        String8 environment_state = settings.manifest_only ? S8("not-executed-manifest-only") : S8("explicit-replacement-in-environment.tsv");
        String8 project_include_sha256 = settings.project_include.length ? settings.project_sha256 : S8("");
        String8 dependency_receipt_name = dependency_required ? S8("dependency-receipt.json") : S8("");
        String8 source_dependencies = settings.project_include.length
                                          ? S8("tracked-tests-plus-snapshotted-resource-include-plus-authenticated-project-include-plus-pinned-github-closure")
                                          : S8("tracked-tests-plus-snapshotted-resource-include");
        String8 sysroot = settings.project_include.length ? S8("target-correct-hosted-sdks") : S8("none");
        String8 system_include = settings.project_include.length ? S8("target-correct-libc-project-include") : S8("none");
        String8 flags = settings.project_include.length
                            ? S8("-c -g0 -v -fwrapv -fno-strict-aliasing -funsigned-char -fverify-codegen -nostdinc -isystem RESOURCE_SNAPSHOT -isystem TARGET_MUSL_INCLUDE -isystem MUSL_INCLUDE")
                            : S8("-c -g0 -v -fwrapv -fno-strict-aliasing -funsigned-char -fverify-codegen -nostdinc -isystem RESOURCE_SNAPSHOT");
        // TCC 0.9.28rc leaks the const qualifier of a `String8 const` passed
        // through `...` onto `settings` and then rejects `settings.rows = ...`
        // as a read-only assignment, which breaks the build.sh bootstrap.
        // Passing non-const copies keeps the driver compiling with TCC.
        String8 archived_input_sha256 = nrc_archived_input_sha256;
        String8 archived_fixture_map_sha256 = nrc_archived_fixture_map_sha256;
        String8 archived_row_sha256 = nrc_archived_row_sha256;
        String8 metadata = string_format(arena, S8("version=2\nkind=object-coverage\nidentity_hash=sha256\nrow_artifact_hash=buster_hash_64-noncryptographic\n"
            "support_contract=docs/native-retirement-support-v1.tsv\nsupport_contract_sha256={S8}\n"
            "supported_gap_ledger=docs/native-retirement-supported-gaps-v1.tsv\nsupported_gap_ledger_sha256={S8}\n"
            "applicability_ledger=docs/native-retirement-applicability-v1.tsv\napplicability_ledger_sha256={S8}\n"
            "applicability_ledger_entries={u64}\nnon_object_control_obligation=registered-non-object-control\n"
            "compiler_revision_claim={S8}\nbaseline_revision_claim={S8}\ncompiler_hash={u64}\ncompiler_bytes={u64}\n"
            "compiler_sha256={S8}\nbaseline_hash={u64}\nbaseline_bytes={u64}\nbaseline_sha256={S8}\n"
            "cpu={S8}\nresource_include_sha256={S8}\nproject_include_sha256={S8}\n"
            "dependency_manifest=docs/native-retirement-dependencies-v1.json\ndependency_manifest_sha256={S8}\n"
            "dependency_snapshot=docs/native-retirement-repository-sources-v1.json\ndependency_snapshot_sha256={S8}\n"
            "dependency_resolved_descriptor_sha256={S8}\n"
            "dependency_receipt={S8}\ndependency_receipt_sha256={S8}\n"
            "dependency_project_include_sha256={S8}\ndependency_ledger_sha256={S8}\n"
            "archived_input_identity_sha256={S8}\narchived_fixture_map_sha256={S8}\narchived_row_identity_sha256={S8}\n"
            "sysroot={S8}\nsystem_include={S8}\ninputs={u64}\nsubjects={u64}\nrows={u64}\n"
            "profile={S8}\nsupported_gap_count={u64}\nsupported_gap_sha256={S8}\n"
            "fixture_filter={S8}\ntarget_filter={S8}\nshard_index={u32}\nshard_count={u32}\nmanifest_only={u32}\ntimeout_seconds={u32}\n"
            "function_evidence=all-observed-fallbacks-plus-first-fatal-diagnostic\n"
            "fixture_flags=exact-path-recipes-in-inputs.tsv\nsource_dependencies={S8}\n"
            "environment={S8}\nunfrozen_dependencies={S8}\n"
            "flags={S8}\n"),
            settings.contract_sha256, settings.supported_gap_ledger_sha256, settings.applicability_ledger_sha256,
            settings.applicability_count,
            settings.compiler_revision, settings.baseline_revision, compiler_hash, compiler_bytes, compiler_sha256,
            baseline_hash, baseline_bytes, baseline_sha256, settings.cpu, settings.resource_sha256, project_include_sha256,
            settings.dependency_manifest_sha256, settings.dependency_snapshot_sha256,
            settings.dependency_resolved_descriptor_sha256, dependency_receipt_name, settings.dependency_receipt_sha256,
            settings.dependency_project_sha256, settings.dependency_ledger_sha256, archived_input_sha256,
            archived_fixture_map_sha256, archived_row_sha256, sysroot, system_include, input_count, subject_count,
            groups * BUSTER_ARRAY_LENGTH(nrc_allocators), profile, 192,
            S8("0f531b1cf7c7922ea891e15703971bcb2ddf95f398f628e0b2681831d7cbf81e"),
            settings.fixture_filter, settings.target_filter,
            settings.shard_index, settings.shard_count, (u32)settings.manifest_only, settings.child.timeout_seconds,
            source_dependencies, environment_state, dependency_state, flags);
        d_write(&settings.child, path_join(arena, settings.child.out, S8("manifest.txt")), metadata);
        settings.rows = nrc_open(&settings, S8("results.tsv"));
        settings.counters = nrc_open(&settings, S8("fallback-counters.tsv"));
        settings.functions = nrc_open(&settings, S8("fallback-functions.tsv"));
        settings.skips = nrc_open(&settings, S8("applicability-skips.tsv"));
        if (settings.rows && settings.counters && settings.functions && settings.skips)
        {
            fprintf(settings.rows, "row\tgroup\tdisposition\tkind\tstatus\tcounters_valid\ttarget_identity_valid\tfunction_records_valid\t"
                                   "functions\tfallbacks\tbaseline_functions\tcpu\tcpu_features\tobject_bytes\tobject_hash\tobject_sha256\n");
            fprintf(settings.counters, "row\ttelemetry\n");
            fprintf(settings.functions, "row\trecord_valid\ttelemetry\n");
            fprintf(settings.skips, "row\tgroup\tfixture\ttarget\tallocator\tapplicability\treason\n");
            if (!settings.manifest_only && !settings.child.io_failed) { nrc_manifest(&settings, inputs, input_count, true); }
        }
        nrc_close(&settings, settings.rows);
        nrc_close(&settings, settings.counters);
        nrc_close(&settings, settings.functions);
        nrc_close(&settings, settings.skips);
        nrc_close(&settings, settings.child.report);
        bool complete = !settings.manifest_only && settings.selected_groups == groups && !settings.child.io_failed;
        String8 summary = string_format(arena, S8("version=1\ngroups={u64}\nexecuted_groups={u64}\ncomplete_cross_product={u32}\n"
            "nonexecuted_rows={u64}\nbaseline_unresolved={u64}\nsupported_native_gaps={u64}\nstrict_successes={u64}\nstrict_empty_units={u64}\nprotocol_failures={u64}\nio_failed={u32}\nretirement_accepted=0\n"),
            groups, settings.selected_groups, (u32)complete, settings.skipped_rows, settings.baseline_failures, settings.gaps, settings.strict_successes, settings.strict_empty,
            settings.failures, (u32)settings.child.io_failed);
        d_write(&settings.child, path_join(arena, settings.child.out, S8("summary.txt")), summary);
        string_print(S8("NATIVE_RETIREMENT_CENSUS groups={u64}/{u64} nonexecuted_rows={u64} baseline_unresolved={u64} gaps={u64} strict_successes={u64} failures={u64} io_failed={u32}\n"),
            settings.selected_groups, groups, settings.skipped_rows, settings.baseline_failures, settings.gaps, settings.strict_successes, settings.failures, (u32)settings.child.io_failed);
        result = settings.child.io_failed || settings.baseline_failures || settings.gaps || settings.failures ||
                 (!settings.manifest_only && !settings.selected_groups) ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
    }
    return result;
}
