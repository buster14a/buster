// Included by build.c after differential.c: this is build-driver orchestration,
// not a second executable. nrc_inventory freezes tracked test inputs and records
// exclusions; nrc_manifest freezes the complete cross product before execution;
// nrc_group compares a direct baseline with strict MIR using bounded d_observe
// children. Opt-in CODEGEN_FALLBACK_FUNCTION rows attribute every observed
// fallback; fatal diagnostics and aggregate counters are retained too. This is
// object coverage, never execution or final retirement acceptance.

#define NRC_ALLOCATOR(name, value) S8_INITIALIZER(name),
BUSTER_GLOBAL_LOCAL String8 const nrc_allocators[] = {BUSTER_CODEGEN_ALLOCATORS(NRC_ALLOCATOR)};
#undef NRC_ALLOCATOR

BUSTER_GLOBAL_LOCAL String8 const nrc_targets[] = {
    S8_INITIALIZER("x86_64-unknown-linux-gnu"), S8_INITIALIZER("aarch64-unknown-linux-gnu"),
    S8_INITIALIZER("x86_64-pc-windows-msvc"), S8_INITIALIZER("aarch64-pc-windows-msvc"),
    S8_INITIALIZER("x86_64-apple-macos"), S8_INITIALIZER("aarch64-apple-macos"),
    S8_INITIALIZER("x86_64-linux-android"), S8_INITIALIZER("aarch64-linux-android"),
    S8_INITIALIZER("x86_64-apple-ios"), S8_INITIALIZER("aarch64-apple-ios"),
    S8_INITIALIZER("x86_64-unknown-uefi"), S8_INITIALIZER("aarch64-unknown-uefi"),
};

typedef struct NrcInput NrcInput;
struct NrcInput { String8 path; String8 role; u64 hash; u64 bytes; };
typedef struct NrcStatistics NrcStatistics;
struct NrcStatistics { u32 functions; u32 fallbacks; bool valid; };
typedef struct NrcSettings NrcSettings;
struct NrcSettings
{
    DSettings child;
    String8 baseline;
    String8 compiler_revision;
    String8 baseline_revision;
    String8 fixture_filter;
    String8 target_filter;
    String8 cpu;
    String8 snapshot;
    FILE* rows;
    FILE* counters;
    FILE* functions;
    u32 shard_index;
    u32 shard_count;
    u64 selected_groups;
    u64 baseline_failures;
    u64 gaps;
    u64 failures;
    u64 strict_successes;
    u64 strict_empty;
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

BUSTER_GLOBAL_LOCAL String8 nrc_role(String8 path)
{
    // Exact rejection-fixture identities, not keyword matching: for example,
    // basic_c_negative_constant_widening.c is a positive compiler regression.
    String8 rejected[] = {
        S8("tests/basic_c_invalid_labels.c"), S8("tests/basic_c_invalid_asm_goto.c"),
        S8("tests/basic_c_invalid_bit_field_width.c"), S8("tests/basic_c_preprocessor_error.c"),
        S8("tests/self_host_bootstrap_invalid.c"), S8("tests/differential/reject_syntax.c"),
        S8("tests/differential/reject_type.c"),
    };
    String8 result = string_ends_with_sequence(path, S8(".c")) ? S8("subject") : S8("support-file");
    if (string_ends_with_sequence(path, S8(".bbb"))) { result = S8("dormant-custom-language"); }
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(rejected); index += 1)
    {
        if (string_equal(path, rejected[index])) { result = S8("negative-diagnostic-fixture"); }
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

BUSTER_GLOBAL_LOCAL NrcInput* nrc_inventory(NrcSettings* settings, u64* count_out)
{
    Arena* arena = settings->child.arena;
    String8 git = executable_resolve_in_path(arena, S8("git"));
    String8 command[] = {git, S8("ls-files"), S8("-z"), S8("--"), S8("tests")};
    DObservation listed = d_observe(&settings->child, (SliceString8)BUSTER_ARRAY_TO_SLICE(command),
                                    path_join(arena, settings->child.out, S8("tracked-inputs")));
    u64 count = 0;
    for (u64 index = 0; index < listed.output.length; index += 1) { count += listed.output.pointer[index] == 0; }
    bool valid = d_success(listed) && count && listed.output.pointer[listed.output.length - 1] == 0;
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
                inputs[at++] = (NrcInput){.path = string_duplicate_arena(arena, path, true), .role = nrc_role(path)};
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
        fprintf(manifest, "path\trole\tbytes\tbuster_hash_64\n");
        for (u64 index = 0; valid && index < count; index += 1)
        {
            NrcInput* input = inputs + index;
            valid = path_exists(arena, input->path) && (index == 0 || !string_equal(input->path, inputs[index - 1].path));
            if (valid)
            {
                TemporalArena temporary = scratch_begin(&arena, 1);
                // Freeze support headers with the translation units; no reads
                // from changing original test paths are needed during rows.
                String8 destination = path_join(temporary.arena, settings->snapshot, input->path);
                make_directory_recursive(temporary.arena, path_parent(temporary.arena, destination));
                ByteSlice bytes = file_read(temporary.arena, input->path, (FileReadOptions){0});
                OsFileDescriptor* source = os_file_open(input->path, (OpenFlags){.read = 1}, (OpenPermissions){.read = 1});
                valid = source != 0;
                if (source)
                {
                    valid = os_file_get_size(source) == bytes.length;
                    valid &= os_file_close(source);
                }
                if (valid) { d_write(&settings->child, destination, BYTE_SLICE_TO_STRING(8, bytes)); }
                input->bytes = bytes.length;
                input->hash = buster_hash_64(bytes.pointer, bytes.length);
                fprintf(manifest, "%.*s\t%.*s\t%llu\t%llu\n", (int)input->path.length, input->path.pointer,
                        (int)input->role.length, input->role.pointer, (unsigned long long)input->bytes, (unsigned long long)input->hash);
                scratch_end(temporary);
            }
        }
        nrc_close(settings, manifest);
    }
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

BUSTER_GLOBAL_LOCAL NrcStatistics nrc_statistics(String8 output, String8 allocator)
{
    NrcStatistics result = {0};
    u32 matches = 0;
    String8 line = {0};
    while (text_next_line(&output, &line))
    {
        if (string_starts_with_sequence(line, S8("CODEGEN ")))
        {
            matches += 1;
            result.valid = nrc_u32_field(line, S8("functions"), &result.functions) &&
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
                    result.valid &= string_equal(value, allocator);
                }
                cursor = end < line.length ? end + 1 : end;
            }
            result.valid &= allocator_matches == 1 && result.fallbacks <= result.functions;
        }
    }
    result.valid &= matches == 1;
    return result;
}

BUSTER_GLOBAL_LOCAL void nrc_counters(NrcSettings* settings, u64 row, String8 output)
{
    String8 line = {0};
    while (text_next_line(&output, &line))
    {
        if (string_starts_with_sequence(line, S8("CODEGEN_FALLBACK")) &&
            !string_starts_with_sequence(line, S8("CODEGEN_FALLBACK_FUNCTION ")) &&
            !string_starts_with_sequence(line, S8("CODEGEN_FALLBACK_CENSUS ")))
        {
            fprintf(settings->counters, "%llu\t%.*s\n", (unsigned long long)row, (int)line.length, line.pointer);
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
            // A census row compiles one TU, so function IDs strictly increase.
            // They need not be dense: declarations need not have lowered bodies.
            record_valid &= reason_found && (records == 0 || function > previous_function);
            valid &= record_valid;
            previous_function = function;
            records += 1;
            if (settings->functions)
            {
                fprintf(settings->functions, "%llu\t%u\t%.*s\n", (unsigned long long)row, (unsigned)record_valid, (int)line.length, line.pointer);
            }
        }
    }
    return valid && summaries == 1 && declared == records && records == expected;
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
        String8 cpu = string_format(temporary.arena, S8("-mcpu={S8}"), settings->cpu);
        String8 include = string_format(temporary.arena, S8("-I{S8}/tests"), settings->snapshot);
        String8 command[] = {mode ? settings->child.ide : settings->baseline, S8("cc"), S8("-c"), S8("-g0"), S8("-v"),
            S8("-fwrapv"), S8("-fno-strict-aliasing"), S8("-funsigned-char"), S8("-target"), nrc_targets[target], cpu,
            pic ? S8("-fPIC") : S8("-fno-pic"), frontend ? S8("-ffrontend-ssa") : S8("-fno-frontend-ssa"),
            allocator, S8("-fverify-codegen"), mode ? S8("-fno-machine-fallback") : S8("-fmachine-fallback"),
            include, source, S8("-o"), object, S8("-fcodegen-fallback-census")};
        SliceString8 argv = {.pointer = command, .length = BUSTER_ARRAY_LENGTH(command) - (mode == 0)};
        DObservation observed = d_observe(&child, argv, prefix);
        NrcStatistics statistics = nrc_statistics(observed.output, nrc_allocators[mode]);
        bool records_valid = !mode || nrc_function_records(settings, group * BUSTER_ARRAY_LENGTH(nrc_allocators) + mode,
                                                          observed.output, nrc_allocators[mode], statistics.fallbacks);
        nrc_counters(settings, group * BUSTER_ARRAY_LENGTH(nrc_allocators) + mode, observed.output);
        u64 object_hash = 0, object_bytes = 0;
        bool artifact = build_artifact_fanout_hash_file(temporary.arena, object, &object_hash, &object_bytes);
        bool success = d_success(observed) && artifact && statistics.valid && records_valid && statistics.fallbacks == 0 &&
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
            settings->failures += baseline_supported && (!statistics.valid || !records_valid);
        }
        fprintf(settings->rows, "%llu\t%llu\t%.*s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%llu\t%llu\n",
            (unsigned long long)(group * BUSTER_ARRAY_LENGTH(nrc_allocators) + mode), (unsigned long long)group,
            (int)disposition.length, disposition.pointer, (unsigned)observed.kind, observed.status,
            (unsigned)statistics.valid, (unsigned)records_valid, statistics.functions, statistics.fallbacks, baseline.functions,
            (unsigned long long)object_bytes, (unsigned long long)object_hash);
        settings->child.io_failed |= child.io_failed || fflush(settings->rows) != 0 || fflush(settings->counters) != 0 || fflush(settings->functions) != 0;
        scratch_end(temporary);
    }
    settings->selected_groups += 1;
}

BUSTER_GLOBAL_LOCAL u64 nrc_manifest(NrcSettings* settings, NrcInput* inputs, u64 count, bool run)
{
    FILE* manifest = run ? 0 : nrc_open(settings, S8("rows.tsv"));
    if (manifest) { fprintf(manifest, "row\tgroup\tfixture\ttarget\tallocator\tfrontend_ssa\tPIC\tselected\n"); }
    u64 group = 0;
    for (u64 input = 0; input < count; input += 1)
    {
        if (string_equal(inputs[input].role, S8("subject")))
        {
            for (u32 target = 0; target < BUSTER_ARRAY_LENGTH(nrc_targets); target += 1)
            {
                for (u32 frontend = 0; frontend < 2; frontend += 1)
                {
                    for (u32 pic = 0; pic < 2; pic += 1)
                    {
                        bool selected = nrc_selected(settings, inputs[input].path, nrc_targets[target], group);
                        if (run && selected && !settings->child.io_failed)
                        {
                            nrc_group(settings, inputs[input], target, frontend, pic, group);
                        }
                        if (manifest)
                        {
                            for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(nrc_allocators); mode += 1)
                            {
                                fprintf(manifest, "%llu\t%llu\t%.*s\t%.*s\t%.*s\t%u\t%u\t%u\n",
                                    (unsigned long long)(group * BUSTER_ARRAY_LENGTH(nrc_allocators) + mode), (unsigned long long)group,
                                    (int)inputs[input].path.length, inputs[input].path.pointer, (int)nrc_targets[target].length,
                                    nrc_targets[target].pointer, (int)nrc_allocators[mode].length, nrc_allocators[mode].pointer,
                                    frontend, pic, (unsigned)selected);
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
    failures += !string_equal(nrc_role(S8("tests/basic_if_else.bbb")), S8("dormant-custom-language"));
    failures += !string_equal(nrc_role(S8("tests/basic_c_guarded_include.h")), S8("support-file"));
    failures += nrc_field_safe(S8("bad\tpath")) || nrc_revision_valid(S8("main"));
    failures += !nrc_revision_valid(S8("641cd88d33decfd56fa2da9a960c6ac075935a71"));
    NrcStatistics valid = nrc_statistics(S8("CODEGEN functions=4 allocator=fast fallback_functions=2\n"), S8("fast"));
    failures += !valid.valid || valid.functions != 4 || valid.fallbacks != 2;
    String8 invalid[] = {S8(""), S8("CODEGEN functions=4 allocator=none fallback_functions=0\n"),
        S8("CODEGEN functions=4 allocator=fast fallback_functions=5\n"),
        S8("CODEGEN functions=-1 allocator=fast fallback_functions=0\n"),
        S8("CODEGEN functions=4294967296 allocator=fast fallback_functions=0\n"),
        S8("CODEGEN functions=4 functions=4 allocator=fast fallback_functions=0\n"),
        S8("CODEGEN functions=4 allocator=fast fallback_functions=0\nCODEGEN functions=4 allocator=fast fallback_functions=0\n")};
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
    failures += nrc_hex_field(S8("a")) || nrc_hex_field(S8("0g")) || !nrc_hex_field(S8("-"));
    for (u64 group = 0; group < 101; group += 1)
    {
        u32 selected = 0;
        for (u32 shard = 0; shard < 7; shard += 1)
        {
            NrcSettings settings = {.shard_index = shard, .shard_count = 7};
            selected += nrc_selected(&settings, S8("tests/basic_c_operations.c"), nrc_targets[0], group);
        }
        failures += selected != 1;
    }
    string_print(S8("NATIVE_RETIREMENT_CENSUS_SELF_TEST failures={u32}\n"), failures);
    return failures;
}

BUSTER_GLOBAL_LOCAL ProcessResult native_retirement_census_main(Arena* arena, SliceString8 arguments)
{
    NrcSettings settings = {.child = {.arena = arena, .ide = S8("build/Release/ide"),
        .out = S8("build/native-retirement-census"), .timeout_seconds = 30, .verify = true},
        .shard_count = 1, .cpu = S8("baseline")};
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
        target_found |= string_equal(settings.target_filter, nrc_targets[index]);
    }
    valid &= target_found;
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    if (!valid)
    {
        string_print(S8("usage: native_retirement_census --compiler-revision <40-hex> [--ide path] [--baseline-ide path --baseline-revision <40-hex>] "
                        "[--out new-directory] [--fixture substring] [--target triple] [--cpu model] [--shard-index N --shard-count N] "
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
        u64 compiler_hash = 0, compiler_bytes = 0, baseline_hash = 0, baseline_bytes = 0;
        if (!settings.manifest_only && !settings.child.io_failed)
        {
            settings.child.ide = os_path_absolute(arena, settings.child.ide, true);
            settings.baseline = os_path_absolute(arena, settings.baseline, true);
            settings.child.io_failed |= !build_artifact_fanout_hash_file(arena, settings.child.ide, &compiler_hash, &compiler_bytes) ||
                                       !build_artifact_fanout_hash_file(arena, settings.baseline, &baseline_hash, &baseline_bytes);
            // The existing fanout helper preserves executable permissions and
            // verifies both bytes and fingerprint before admitting the copy.
            String8 candidate_copy = path_join(arena, settings.child.out, S8("candidate-ide.exe"));
            String8 baseline_copy = path_join(arena, settings.child.out, S8("baseline-ide.exe"));
            if (!settings.child.io_failed)
            {
                settings.child.io_failed |= !build_artifact_fanout_snapshot(arena, settings.child.ide, candidate_copy, compiler_hash, compiler_bytes) ||
                                           !build_artifact_fanout_snapshot(arena, settings.baseline, baseline_copy, baseline_hash, baseline_bytes);
                settings.child.ide = candidate_copy;
                settings.baseline = baseline_copy;
            }
        }
        String8 metadata = string_format(arena, S8("version=1\nkind=object-coverage\nhash_algorithm=buster_hash_64-noncryptographic\n"
            "compiler_revision_claim={S8}\nbaseline_revision_claim={S8}\ncompiler_hash={u64}\ncompiler_bytes={u64}\n"
            "baseline_hash={u64}\nbaseline_bytes={u64}\ncpu={S8}\ninputs={u64}\nrows={u64}\n"
            "fixture_filter={S8}\ntarget_filter={S8}\nshard_index={u32}\nshard_count={u32}\nmanifest_only={u32}\ntimeout_seconds={u32}\n"
            "function_evidence=all-observed-fallbacks-plus-first-fatal-diagnostic\nflags=-c -g0 -v -fwrapv -fno-strict-aliasing -funsigned-char -fverify-codegen\n"),
            settings.compiler_revision, settings.baseline_revision, compiler_hash, compiler_bytes, baseline_hash, baseline_bytes, settings.cpu,
            input_count, groups * BUSTER_ARRAY_LENGTH(nrc_allocators), settings.fixture_filter, settings.target_filter,
            settings.shard_index, settings.shard_count, (u32)settings.manifest_only, settings.child.timeout_seconds);
        d_write(&settings.child, path_join(arena, settings.child.out, S8("manifest.txt")), metadata);
        settings.rows = nrc_open(&settings, S8("results.tsv"));
        settings.counters = nrc_open(&settings, S8("fallback-counters.tsv"));
        settings.functions = nrc_open(&settings, S8("fallback-functions.tsv"));
        if (settings.rows && settings.counters && settings.functions)
        {
            fprintf(settings.rows, "row\tgroup\tdisposition\tkind\tstatus\tcounters_valid\tfunction_records_valid\tfunctions\tfallbacks\tbaseline_functions\tobject_bytes\tobject_hash\n");
            fprintf(settings.counters, "row\ttelemetry\n");
            fprintf(settings.functions, "row\trecord_valid\ttelemetry\n");
            if (!settings.manifest_only && !settings.child.io_failed) { nrc_manifest(&settings, inputs, input_count, true); }
        }
        nrc_close(&settings, settings.rows);
        nrc_close(&settings, settings.counters);
        nrc_close(&settings, settings.functions);
        nrc_close(&settings, settings.child.report);
        bool complete = !settings.manifest_only && settings.selected_groups == groups && !settings.child.io_failed;
        String8 summary = string_format(arena, S8("version=1\ngroups={u64}\nexecuted_groups={u64}\ncomplete_cross_product={u32}\n"
            "baseline_unresolved={u64}\nsupported_native_gaps={u64}\nstrict_successes={u64}\nstrict_empty_units={u64}\nprotocol_failures={u64}\nio_failed={u32}\nretirement_accepted=0\n"),
            groups, settings.selected_groups, (u32)complete, settings.baseline_failures, settings.gaps, settings.strict_successes, settings.strict_empty,
            settings.failures, (u32)settings.child.io_failed);
        d_write(&settings.child, path_join(arena, settings.child.out, S8("summary.txt")), summary);
        string_print(S8("NATIVE_RETIREMENT_CENSUS groups={u64}/{u64} baseline_unresolved={u64} gaps={u64} strict_successes={u64} failures={u64} io_failed={u32}\n"),
            settings.selected_groups, groups, settings.baseline_failures, settings.gaps, settings.strict_successes, settings.failures, (u32)settings.child.io_failed);
        result = settings.child.io_failed || settings.baseline_failures || settings.gaps || settings.failures ||
                 (!settings.manifest_only && !settings.selected_groups) ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
    }
    return result;
}
