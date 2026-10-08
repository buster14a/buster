// Bounded stock-header LZCNT conformance lane, included by clang_suite.c.
// clang_suite_intrinsics is the entry point; capture writes argv/stdout/stderr/status,
// resource verification hashes every pinned clang/lib/Headers worktree blob, and
// disassembly checks baseline safety or Clang native lowering. This is not a full census.
#define BUSTER_CLANG_SUITE_LZCNT_POLICY_NONE 0
#define BUSTER_CLANG_SUITE_LZCNT_POLICY_FORBID 1
#define BUSTER_CLANG_SUITE_LZCNT_POLICY_REQUIRE_EACH 2

BUSTER_GLOBAL_LOCAL bool clang_suite_intrinsics_process_success(ClangSuiteCommand command)
{
    bool result = command.result == PROCESS_RESULT_SUCCESS && !command.launch_failed && !command.timed_out &&
                  !command.output_truncated && !command.capture_failed && !command.cleanup_failed;
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_intrinsics_version_matches(String8 output)
{
    u64 end = 0;
    while (end < output.length && output.pointer[end] != '\n')
    {
        end += 1;
    }
    String8 line = string_slice(output, 0, end);
    String8 expected = S8("clang version 23.1.2");
    bool valid = string_starts_with_sequence(line, expected) && line.length > expected.length;
    if (valid)
    {
        char8 boundary = line.pointer[expected.length];
        valid = boundary == ' ' || boundary == '(';
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_intrinsics_capture(Arena* arena, String8 working_directory, String8 results, String8 label,
                                                         SliceString8 arguments, ClangSuiteCommand* command_out)
{
    *command_out = clang_suite_command(arena, working_directory, arguments);
    String8List argument_lines = {0};
    for (u64 i = 0; i < arguments.length; i += 1)
    {
        string8_list_push(arena, &argument_lines, arguments.pointer[i]);
        string8_list_push(arena, &argument_lines, S8("\n"));
    }
    String8 argument_text = string_join_arena(arena, string8_list_to_slice(arena, argument_lines), true);
    String8 status = string_format(arena, S8("status={S8}\nportable_result={u32}\nplatform_status={u32}\nlaunch_failed={u32}\n"
                                             "timed_out={u32}\noutput_truncated={u32}\ncapture_failed={u32}\ncleanup_failed={u32}\n"),
                                   clang_suite_smoke_status(*command_out), (u32)command_out->result, command_out->platform_status,
                                   (u32)command_out->launch_failed, (u32)command_out->timed_out, (u32)command_out->output_truncated,
                                   (u32)command_out->capture_failed, (u32)command_out->cleanup_failed);
    bool recorded = clang_suite_write(arena, path_join(arena, results, string_format(arena, S8("{S8}.argv"), label)), argument_text) &&
                    clang_suite_write(arena, path_join(arena, results, string_format(arena, S8("{S8}.stdout"), label)), command_out->output) &&
                    clang_suite_write(arena, path_join(arena, results, string_format(arena, S8("{S8}.stderr"), label)), command_out->error) &&
                    clang_suite_write(arena, path_join(arena, results, string_format(arena, S8("{S8}.status"), label)), status);
    return recorded;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_intrinsics_verify_resource_headers(Arena* arena, String8 checkout, String8 results,
                                                                        u64* file_count_out, String8* manifest_digest_out)
{
    *file_count_out = 0;
    *manifest_digest_out = {0};
    String8 tree_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("ls-tree"), S8("-r"), S8("-z"), S8("--full-tree"),
                                S8(BUSTER_CLANG_SUITE_COMMIT), S8("--"), S8("clang/lib/Headers")};
    ClangSuiteCommand tree;
    bool recorded = clang_suite_intrinsics_capture(arena, checkout, results, S8("resource-header-tree"),
                                                    (SliceString8)BUSTER_ARRAY_TO_SLICE(tree_arguments), &tree);
    bool valid = recorded && clang_suite_intrinsics_process_success(tree) && tree.output.length != 0;
    String8List rows = {0};
    string8_list_push(arena, &rows, S8("BUSTER_CLANG_SUITE_RESOURCE_HEADERS_V1\nmode\tblob\tworktree_blob\tpath\tstatus\n"));
    String8 previous = {0};
    u64 cursor = 0;
    while (valid && cursor < tree.output.length)
    {
        u64 end = cursor;
        while (end < tree.output.length && tree.output.pointer[end] != '\0')
        {
            end += 1;
        }
        String8 record = string_slice(tree.output, cursor, end);
        bool record_valid = end < tree.output.length && record.length > 53 && record.pointer[6] == ' ' &&
                            string_equal(string_slice(record, 7, 11), S8("blob")) && record.pointer[11] == ' ' &&
                            record.pointer[52] == '\t';
        if (record_valid)
        {
            String8 mode = string_slice(record, 0, 6);
            String8 expected = string_slice(record, 12, 52);
            String8 path = string_slice(record, 53, record.length);
            bool mode_valid = string_equal(mode, S8("100644")) || string_equal(mode, S8("100755")) ||
                              string_equal(mode, S8("120000"));
            record_valid = mode_valid && clang_suite_sha_valid(expected) &&
                           string_starts_with_sequence(path, S8("clang/lib/Headers/")) && clang_suite_safe_path(path);
            if (record_valid && previous.length)
            {
                u64 length = previous.length < path.length ? previous.length : path.length;
                int order = memcmp(previous.pointer, path.pointer, (size_t)length);
                record_valid = order < 0 || (order == 0 && previous.length < path.length);
            }
            if (record_valid)
            {
                String8 hash_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("hash-object"), S8("--no-filters"),
                                            S8("--"), path};
                ClangSuiteCommand hash = clang_suite_command(arena, checkout, (SliceString8)BUSTER_ARRAY_TO_SLICE(hash_arguments));
                String8 actual = quickjs_trim_ascii_space(hash.output);
                bool hash_valid = clang_suite_intrinsics_process_success(hash) && string_equal(actual, expected);
                string8_list_push(arena, &rows,
                                  string_format(arena, S8("{S8}\t{S8}\t{S8}\t{S8}\t{S8}\n"), mode, expected, actual, path,
                                                clang_suite_smoke_status(hash)));
                *file_count_out += 1;
                previous = path;
                valid = hash_valid && valid;
            }
            else
            {
                valid = false;
            }
        }
        else
        {
            valid = false;
        }
        cursor = end + 1;
    }
    valid = *file_count_out != 0 && valid;
    if (valid)
    {
        String8 manifest = string_join_arena(arena, string8_list_to_slice(arena, rows), true);
        *manifest_digest_out = stage_object_sha256_bytes(arena, (u8*)manifest.pointer, manifest.length);
        valid = clang_suite_write(arena, path_join(arena, results, S8("resource-headers.tsv")), manifest) && valid;
    }
    else
    {
        string_print(S8("error: pinned Clang resource-header worktree does not match all {u64} source blobs\n"),
                     (u64)BUSTER_CLANG_SUITE_INTRINSICS_HEADER_FILES);
        String8 partial = string_join_arena(arena, string8_list_to_slice(arena, rows), true);
        clang_suite_write(arena, path_join(arena, results, S8("resource-headers.partial.tsv")), partial);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_intrinsics_disassembly_has_lzcnt(String8 output, u32 policy)
{
    bool valid = false;
    if (policy == BUSTER_CLANG_SUITE_LZCNT_POLICY_NONE)
    {
        valid = output.length != 0;
    }
    else if (policy == BUSTER_CLANG_SUITE_LZCNT_POLICY_FORBID ||
             policy == BUSTER_CLANG_SUITE_LZCNT_POLICY_REQUIRE_EACH)
    {
        String8 symbols[] = {S8("<zen5_lzcnt_probe_u16_macro>:"), S8("<zen5_lzcnt_probe_u32_function>:"),
                             S8("<zen5_lzcnt_probe_u32_alias>:"), S8("<zen5_lzcnt_probe_u64_macro>:"),
                             S8("<zen5_lzcnt_probe_u64_alias>:")};
        bool symbols_seen[BUSTER_ARRAY_LENGTH(symbols)] = {0};
        bool instructions_seen[BUSTER_ARRAY_LENGTH(symbols)] = {0};
        u64 cursor = 0;
        u32 current = UINT32_MAX;
        valid = output.length != 0;
        while (valid && cursor < output.length)
        {
            u64 line_end = cursor;
            while (line_end < output.length && output.pointer[line_end] != '\n')
            {
                line_end += 1;
            }
            String8 line = string_slice(output, cursor, line_end);
            bool label = string_first_sequence(line, S8("<")) != BUSTER_STRING_NO_MATCH &&
                         string_first_sequence(line, S8(">")) != BUSTER_STRING_NO_MATCH;
            if (label)
            {
                current = UINT32_MAX;
                for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(symbols); i += 1)
                {
                    if (string_first_sequence(line, symbols[i]) != BUSTER_STRING_NO_MATCH)
                    {
                        current = i;
                        symbols_seen[i] = true;
                    }
                }
            }
            else if (string_first_sequence(line, S8("lzcnt")) != BUSTER_STRING_NO_MATCH)
            {
                if (current < BUSTER_ARRAY_LENGTH(symbols))
                {
                    instructions_seen[current] = true;
                }
                else if (policy == BUSTER_CLANG_SUITE_LZCNT_POLICY_FORBID)
                {
                    valid = false;
                }
            }
            cursor = line_end + 1;
        }
        if (policy == BUSTER_CLANG_SUITE_LZCNT_POLICY_REQUIRE_EACH)
        {
            for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(symbols); i += 1)
            {
                valid = symbols_seen[i] && instructions_seen[i] && valid;
            }
        }
        else if (policy == BUSTER_CLANG_SUITE_LZCNT_POLICY_FORBID)
        {
            for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(symbols); i += 1)
            {
                valid = symbols_seen[i] && !instructions_seen[i] && valid;
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_intrinsics_disassemble(Arena* arena, String8 working_directory, String8 results, String8 objdump,
                                                             String8 label, String8 object_path, u32 policy)
{
    String8 arguments[] = {objdump, S8("-d"), S8("--no-show-raw-insn"), object_path};
    ClangSuiteCommand command;
    bool recorded = clang_suite_intrinsics_capture(arena, working_directory, results, label,
                                                    (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), &command);
    bool valid = recorded && clang_suite_intrinsics_process_success(command) && command.output.length != 0 &&
                 clang_suite_intrinsics_disassembly_has_lzcnt(command.output, policy);
    if (!valid)
    {
        string_print(S8("error: LZCNT object inspection failed for {S8}\n"), label);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_suite_intrinsics(Arena* arena, String8 checkout, String8 results, String8 ide, String8 clang)
{
    String8 working_directory = clang_suite_directory(arena, S8("."));
    String8 header_directory = path_join(arena, checkout, S8("clang/lib/Headers"));
    String8 fixture_source = os_path_absolute(arena, S8("tools/fixtures/zen5_lzcnt.c"), true);
    String8 fixture_copy = path_join(arena, results, S8("zen5_lzcnt.c"));
    String8 stock_header = path_join(arena, results, S8("stock-immintrin.c"));
    String8 stock_text = S8("#include <immintrin.h>\n");
    String8 objdump = path_join(arena, path_parent(arena, clang), S8("llvm-objdump"));
    bool valid = working_directory.length && header_directory.length && fixture_source.length &&
                 clang_suite_verify_checkout(arena, checkout) && !string_starts_with_sequence(results, path_join(arena, checkout, S8("")));
    String8 fixture = valid ? clang_suite_read(arena, fixture_source) : (String8){0};
    valid = fixture.length && clang_suite_write(arena, fixture_copy, fixture) && valid;
    valid = clang_suite_write(arena, stock_header, stock_text) && valid;

    u64 header_count = 0;
    String8 header_digest = {0};
    bool headers_valid = false;
    if (valid)
    {
        headers_valid = clang_suite_intrinsics_verify_resource_headers(arena, checkout, results, &header_count, &header_digest);
        valid = headers_valid && valid;
    }

    String8 version_arguments[] = {clang, S8("--version")};
    ClangSuiteCommand version;
    bool version_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-version"),
                                                            (SliceString8)BUSTER_ARRAY_TO_SLICE(version_arguments), &version);
    bool version_valid = version_recorded && clang_suite_intrinsics_process_success(version) &&
                         clang_suite_intrinsics_version_matches(version.output);
    valid = version_valid && valid;

    String8 resource_arguments[] = {clang, S8("-print-resource-dir")};
    ClangSuiteCommand resource;
    bool resource_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-resource-dir"),
                                                             (SliceString8)BUSTER_ARRAY_TO_SLICE(resource_arguments), &resource);
    String8 resource_path = quickjs_trim_ascii_space(resource.output);
    String8 resolved_resource_path = resource_path.length ? clang_suite_directory(arena, resource_path) : (String8){0};
    bool resource_valid = resource_recorded && clang_suite_intrinsics_process_success(resource) && resolved_resource_path.length;
    valid = resource_valid && valid;

    String8 header_arguments[] = {clang, S8("-std=gnu11"), S8("-march=znver5"), S8("-I"), header_directory,
                                  S8("-H"), S8("-fsyntax-only"), stock_header};
    ClangSuiteCommand header_trace;
    bool header_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-header-trace"),
                                                           (SliceString8)BUSTER_ARRAY_TO_SLICE(header_arguments), &header_trace);
    String8 immintrin_path = path_join(arena, header_directory, S8("immintrin.h"));
    String8 lzcnt_path = path_join(arena, header_directory, S8("lzcntintrin.h"));
    bool trace_valid = header_recorded && clang_suite_intrinsics_process_success(header_trace) &&
                       string_first_sequence(header_trace.error, immintrin_path) != BUSTER_STRING_NO_MATCH &&
                       string_first_sequence(header_trace.error, lzcnt_path) != BUSTER_STRING_NO_MATCH;
    valid = trace_valid && valid;

    String8 macro_arguments[] = {clang, S8("-std=gnu11"), S8("-march=znver5"), S8("-I"), header_directory,
                                 S8("-dM"), S8("-E"), stock_header};
    ClangSuiteCommand macro_dump;
    bool macro_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-znver5-macros"),
                                                          (SliceString8)BUSTER_ARRAY_TO_SLICE(macro_arguments), &macro_dump);
    bool macros_valid = macro_recorded && clang_suite_intrinsics_process_success(macro_dump) &&
                        string_first_sequence(macro_dump.output, S8("#define __lzcnt16(")) != BUSTER_STRING_NO_MATCH &&
                        string_first_sequence(macro_dump.output, S8("#define __lzcnt64(")) != BUSTER_STRING_NO_MATCH;
    valid = macros_valid && valid;

    String8 ast_arguments[] = {clang, S8("-std=gnu11"), S8("-march=znver5"), S8("-I"), header_directory,
                               S8("-Xclang"), S8("-ast-list"), S8("-fsyntax-only"), fixture_copy};
    ClangSuiteCommand ast;
    bool ast_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-znver5-ast-list"),
                                                        (SliceString8)BUSTER_ARRAY_TO_SLICE(ast_arguments), &ast);
    bool ast_valid = ast_recorded && clang_suite_intrinsics_process_success(ast) &&
                     string_first_sequence(ast.output, S8("__lzcnt32")) != BUSTER_STRING_NO_MATCH &&
                     string_first_sequence(ast.output, S8("_lzcnt_u32")) != BUSTER_STRING_NO_MATCH &&
                     string_first_sequence(ast.output, S8("_lzcnt_u64")) != BUSTER_STRING_NO_MATCH &&
                     string_first_sequence(ast.output, S8("zen5_lzcnt_probe_u16_macro")) != BUSTER_STRING_NO_MATCH;
    valid = ast_valid && valid;

    String8 clang_stock_arguments[] = {clang, S8("-std=gnu11"), S8("-march=znver5"), S8("-I"), header_directory,
                                       S8("-fsyntax-only"), fixture_copy};
    ClangSuiteCommand clang_stock;
    bool clang_stock_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-stock-header-and-calls"),
                                                                (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_stock_arguments), &clang_stock);
    bool clang_stock_valid = clang_stock_recorded && clang_suite_intrinsics_process_success(clang_stock);
    valid = clang_stock_valid && valid;

    String8 buster_stock_arguments[] = {ide, S8("cc"), S8("-std=gnu11"), S8("-march=znver5"), S8("-I"), header_directory,
                                        S8("-fsyntax-only"), fixture_copy};
    ClangSuiteCommand buster_stock;
    bool buster_stock_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("buster-stock-header-and-calls"),
                                                                 (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_stock_arguments), &buster_stock);
    bool buster_stock_valid = buster_stock_recorded && clang_suite_intrinsics_process_success(buster_stock);
    valid = buster_stock_valid && valid;

    String8 clang_baseline_executable = path_join(arena, results, S8("clang-lzcnt-baseline"));
    String8 clang_baseline_arguments[] = {clang, S8("-std=gnu11"), S8("-O0"), S8("-march=x86-64"), S8("-I"), header_directory,
                                          fixture_copy, S8("-o"), clang_baseline_executable};
    ClangSuiteCommand clang_baseline_build;
    bool clang_baseline_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-baseline-build"),
                                                                   (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_baseline_arguments),
                                                                   &clang_baseline_build);
    bool clang_baseline_built = clang_baseline_recorded && clang_suite_intrinsics_process_success(clang_baseline_build);
    valid = clang_baseline_built && valid;
    bool clang_baseline_safe = false;
    bool clang_baseline_ran = false;
    if (clang_baseline_built)
    {
        clang_baseline_safe = clang_suite_intrinsics_disassemble(arena, working_directory, results, objdump,
                                                                 S8("clang-baseline-objdump"), clang_baseline_executable, BUSTER_CLANG_SUITE_LZCNT_POLICY_FORBID);
        if (clang_baseline_safe)
        {
            String8 run_arguments[] = {clang_baseline_executable};
            ClangSuiteCommand run;
            clang_baseline_ran = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-baseline-run"),
                                                                 (SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), &run) &&
                                 clang_suite_intrinsics_process_success(run) &&
                                 string_first_sequence(run.output, S8("ZEN5_LZCNT_CONFORMANCE status=pass")) != BUSTER_STRING_NO_MATCH;
        }
    }
    valid = clang_baseline_safe && clang_baseline_ran && valid;

    String8 allocators[] = {S8("fast"), S8("quality")};
    bool frontend_ssa[] = {false, true};
    u64 buster_runs = 0;
    for (u32 ssa_index = 0; ssa_index < BUSTER_ARRAY_LENGTH(frontend_ssa); ssa_index += 1)
    {
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(allocators); allocator_index += 1)
        {
            String8 ssa_name = frontend_ssa[ssa_index] ? S8("ssa") : S8("no-ssa");
            String8 allocator = allocators[allocator_index];
            String8 label = string_format(arena, S8("buster-baseline-{S8}-{S8}"), ssa_name, allocator);
            String8 executable = path_join(arena, results, string_format(arena, S8("buster-lzcnt-{S8}-{S8}"), ssa_name, allocator));
            String8List arguments = {0};
            string8_list_push(arena, &arguments, ide);
            string8_list_push(arena, &arguments, S8("cc"));
            string8_list_push(arena, &arguments, S8("-std=gnu11"));
            string8_list_push(arena, &arguments, S8("-O0"));
            string8_list_push(arena, &arguments, S8("-march=x86-64"));
            string8_list_push(arena, &arguments, S8("-I"));
            string8_list_push(arena, &arguments, header_directory);
            string8_list_push(arena, &arguments, string_format(arena, S8("-fregister-allocator={S8}"), allocator));
            if (!frontend_ssa[ssa_index])
            {
                string8_list_push(arena, &arguments, S8("-fno-frontend-ssa"));
            }
            string8_list_push(arena, &arguments, fixture_copy);
            string8_list_push(arena, &arguments, S8("-o"));
            string8_list_push(arena, &arguments, executable);
            ClangSuiteCommand build;
            bool build_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, label,
                                                                  string8_list_to_slice(arena, arguments), &build);
            bool built = build_recorded && clang_suite_intrinsics_process_success(build);
            bool safe = built && clang_suite_intrinsics_disassemble(arena, working_directory, results, objdump,
                                                                    string_format(arena, S8("{S8}-objdump"), label), executable, BUSTER_CLANG_SUITE_LZCNT_POLICY_FORBID);
            bool ran = false;
            if (safe)
            {
                String8 run_arguments[] = {executable};
                ClangSuiteCommand run;
                ran = clang_suite_intrinsics_capture(arena, working_directory, results,
                                                      string_format(arena, S8("{S8}-run"), label),
                                                      (SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), &run) &&
                      clang_suite_intrinsics_process_success(run) &&
                      string_first_sequence(run.output, S8("ZEN5_LZCNT_CONFORMANCE status=pass")) != BUSTER_STRING_NO_MATCH;
            }
            buster_runs += ran;
            valid = built && safe && ran && valid;
        }
    }

    String8 clang_object = path_join(arena, results, S8("clang-lzcnt-znver5.o"));
    String8 clang_object_arguments[] = {clang, S8("-std=gnu11"), S8("-O2"), S8("-march=znver5"), S8("-I"), header_directory,
                                        S8("-c"), fixture_copy, S8("-o"), clang_object};
    ClangSuiteCommand clang_object_build;
    bool clang_object_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, S8("clang-znver5-object-build"),
                                                                 (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_object_arguments),
                                                                 &clang_object_build);
    bool native_valid = clang_object_recorded && clang_suite_intrinsics_process_success(clang_object_build) &&
                        clang_suite_intrinsics_disassemble(arena, working_directory, results, objdump,
                                                          S8("clang-znver5-object-objdump"), clang_object, BUSTER_CLANG_SUITE_LZCNT_POLICY_REQUIRE_EACH);
    u32 native_objects_checked = native_valid ? 1 : 0;
    String8List buster_native_records = {0};
    for (u32 ssa_index = 0; ssa_index < BUSTER_ARRAY_LENGTH(frontend_ssa); ssa_index += 1)
    {
        for (u32 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(allocators); allocator_index += 1)
        {
            String8 ssa_name = frontend_ssa[ssa_index] ? S8("ssa") : S8("no-ssa");
            String8 allocator = allocators[allocator_index];
            String8 label = string_format(arena, S8("buster-znver5-{S8}-{S8}"), ssa_name, allocator);
            String8 object_path = path_join(arena, results, string_format(arena, S8("buster-lzcnt-{S8}-{S8}.o"), ssa_name, allocator));
            String8List arguments = {0};
            string8_list_push(arena, &arguments, ide);
            string8_list_push(arena, &arguments, S8("cc"));
            string8_list_push(arena, &arguments, S8("-std=gnu11"));
            string8_list_push(arena, &arguments, S8("-O2"));
            string8_list_push(arena, &arguments, S8("-march=znver5"));
            string8_list_push(arena, &arguments, S8("-I"));
            string8_list_push(arena, &arguments, header_directory);
            string8_list_push(arena, &arguments, string_format(arena, S8("-fregister-allocator={S8}"), allocator));
            if (!frontend_ssa[ssa_index])
            {
                string8_list_push(arena, &arguments, S8("-fno-frontend-ssa"));
            }
            string8_list_push(arena, &arguments, S8("-c"));
            string8_list_push(arena, &arguments, fixture_copy);
            string8_list_push(arena, &arguments, S8("-o"));
            string8_list_push(arena, &arguments, object_path);
            ClangSuiteCommand build;
            bool build_recorded = clang_suite_intrinsics_capture(arena, working_directory, results, label,
                                                                  string8_list_to_slice(arena, arguments), &build);
            bool emitted = build_recorded && clang_suite_intrinsics_process_success(build);
            bool object_valid = emitted && clang_suite_intrinsics_disassemble(
                                              arena, working_directory, results, objdump,
                                              string_format(arena, S8("{S8}-objdump"), label), object_path, BUSTER_CLANG_SUITE_LZCNT_POLICY_NONE);
            native_objects_checked += object_valid;
            string8_list_push(arena, &buster_native_records,
                              string_format(arena, S8("{S8}\t{S8}\n"), label, object_valid ? S8("pass") : emitted ? S8("fail") : S8("not-emitted")));
            native_valid = object_valid && native_valid;
        }
    }
    String8 native_manifest = string_join_arena(arena, string8_list_to_slice(arena, buster_native_records), true);
    bool native_manifest_written = clang_suite_write(arena, path_join(arena, results, S8("buster-znver5-object-status.tsv")),
                                                      native_manifest);
    native_valid = native_manifest_written && native_valid;
    valid = native_valid && valid;

    bool final_checkout_clean = clang_suite_verify_checkout(arena, checkout);
    valid = final_checkout_clean && valid;
    if (!headers_valid)
    {
        string_print(S8("error: intrinsic lane not eligible: pinned clang/lib/Headers worktree blobs failed verification; inspect resource-headers artifacts\n"));
    }
    if (!version_valid)
    {
        string_print(S8("error: intrinsic lane not eligible: compiler is not the pinned Clang 23.1.2 release; inspect clang-version artifacts\n"));
    }
    if (!resource_valid)
    {
        string_print(S8("error: intrinsic lane not eligible: Clang resource directory did not resolve; inspect clang-resource-dir artifacts\n"));
    }
    if (!trace_valid)
    {
        string_print(S8("error: intrinsic lane not eligible: stock immintrin/lzcnt header trace did not resolve to pinned checkout; inspect clang-header-trace artifacts\n"));
    }
    if (!macros_valid)
    {
        string_print(S8("error: intrinsic lane not eligible: target preprocessor did not expose expected LZCNT macros; inspect clang-znver5-macros artifacts\n"));
    }
    if (!ast_valid)
    {
        string_print(S8("error: intrinsic lane not eligible: target AST listing did not expose expected public LZCNT declarations; inspect clang-znver5-ast-list artifacts\n"));
    }
    if (!clang_stock_valid || !buster_stock_valid)
    {
        string_print(S8("error: intrinsic lane not eligible: stock-header calls failed to compile in Clang or Buster; inspect stock-header-and-calls artifacts\n"));
    }
    if (!clang_baseline_safe || !clang_baseline_ran || buster_runs != 4)
    {
        string_print(S8("error: intrinsic lane not eligible: baseline-safe runtime conformance did not pass; inspect baseline build, objdump, and run artifacts\n"));
    }
    if (!native_valid)
    {
        string_print(S8("error: intrinsic lane not eligible: native object build/inspection failed; inspect znver5 object artifacts\n"));
    }
    if (!final_checkout_clean)
    {
        string_print(S8("error: intrinsic lane not eligible: external pinned checkout changed during the run\n"));
    }
    String8 summary = string_format(arena, S8("BUSTER_CLANG_SUITE_LZCNT_CONFORMANCE_V1\n"
                                               "upstream_version={S8}\nupstream_commit={S8}\n"
                                               "profile=gnu-c-x86_64-linux-sysv\nheader_source_root=clang/lib/Headers\n"
                                               "header_files_hashed={u64}\nheader_manifest_sha256={S8}\n"
                                               "clang_version_23_1_2={u32}\nresource_dir_resolved={u32}\nheader_trace_valid={u32}\npreprocessor_macros_valid={u32}\nast_listing_valid={u32}\nclang_stock_calls_valid={u32}\nbuster_stock_calls_valid={u32}\nclang_baseline_runtime_passed={u32}\n"
                                               "public_api_count=5\nbuiltin_count=3\nimmediate_domain_count=0\n"
                                               "runtime_target=x86-64\nruntime_unsafe_instruction_gate=baseline_objects_must_not_contain_lzcnt\n"
                                               "buster_runtime_configurations_passed={u64}\n"
                                               "native_target=znver5\nnative_objects_expected=5\nnative_objects_checked={u32}\n"
                                               "full_immintrin_census=outstanding\nstatus={S8}\n"),
                                  S8(BUSTER_CLANG_SUITE_VERSION), S8(BUSTER_CLANG_SUITE_COMMIT), header_count, header_digest,
                                  (u32)version_valid, (u32)resource_valid, (u32)trace_valid, (u32)macros_valid, (u32)ast_valid,
                                  (u32)clang_stock_valid, (u32)buster_stock_valid, (u32)(clang_baseline_safe && clang_baseline_ran),
                                  buster_runs, native_objects_checked,
                                  valid ? S8("pass") : S8("fail"));
    valid = clang_suite_write(arena, path_join(arena, results, S8("lzcnt-summary.txt")), summary) && valid;
    return valid;
}
