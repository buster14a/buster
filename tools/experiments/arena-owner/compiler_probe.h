#pragma once
// Branch-only in-process driver regression. Included in the IDE only by the
// retained evidence patch; the production driver and lane kernel are unchanged.
#include <stdio.h>

BUSTER_GLOBAL_LOCAL bool owner_position_equal(IrSourcePosition a, IrSourcePosition b)
{
    return a.source == b.source && a.offset == b.offset && a.line == b.line && a.column == b.column;
}
BUSTER_GLOBAL_LOCAL bool owner_location_equal(CompilerDiagnosticLocation a, CompilerDiagnosticLocation b)
{
    return string_equal(a.path, b.path) && string_equal(a.original_path, b.original_path) && a.has_range == b.has_range &&
           a.range.source.value == b.range.source.value && a.range.offset == b.range.offset && a.range.length == b.range.length &&
           owner_position_equal(a.position, b.position) && owner_position_equal(a.original_position, b.original_position);
}
BUSTER_GLOBAL_LOCAL bool owner_diagnostic_equal(CompilerDiagnostic a, CompilerDiagnostic b)
{
    bool equal = a.severity == b.severity && a.note_count == b.note_count && !!a.backend == !!b.backend &&
                 string_equal(a.code, b.code) && string_equal(a.symbol, b.symbol) && string_equal(a.message, b.message) &&
                 owner_location_equal(a.primary, b.primary);
    if (a.note_count == b.note_count)
    {
        for (u32 i = 0; i < a.note_count; i += 1)
        {
            equal = owner_location_equal(a.notes[i].location, b.notes[i].location) &&
                    string_equal(a.notes[i].message, b.notes[i].message) && equal;
        }
    }
    if (a.backend && b.backend)
    {
        CompilerDiagnosticBackend x = *a.backend;
        CompilerDiagnosticBackend y = *b.backend;
        equal = equal && string_equal(x.target, y.target) && string_equal(x.allocator, y.allocator) &&
                string_equal(x.function, y.function) && string_equal(x.opcode, y.opcode) && string_equal(x.operation, y.operation) &&
                string_equal(x.reason, y.reason) && string_equal(x.referenced_symbol, y.referenced_symbol) &&
                x.error_id == y.error_id && x.function_id == y.function_id && x.instruction_id == y.instruction_id &&
                x.opcode_id == y.opcode_id && x.operation_id == y.operation_id;
    }
    return equal;
}
BUSTER_GLOBAL_LOCAL bool owner_result_equal(CompilerDriverResult a, CompilerDriverResult b)
{
    bool equal = a.error == b.error && a.codegen_error == b.codegen_error && a.object_error == b.object_error &&
                 a.native_link.error == b.native_link.error && string_equal(a.native_link.symbol, b.native_link.symbol) &&
                 a.diagnostic_count == b.diagnostic_count && string_equal(a.diagnostic, b.diagnostic) && string_equal(a.warning, b.warning) &&
                 a.tokenizer_error_count == b.tokenizer_error_count && a.tokenizer_warning_count == b.tokenizer_warning_count &&
                 a.parser_diagnostic_count == b.parser_diagnostic_count && a.analysis_diagnostic_count == b.analysis_diagnostic_count;
    if (a.diagnostic_count == b.diagnostic_count)
    {
        for (u32 i = 0; i < a.diagnostic_count; i += 1)
        {
            equal = owner_diagnostic_equal(a.diagnostics[i], b.diagnostics[i]) && equal;
        }
    }
    return equal;
}
BUSTER_GLOBAL_LOCAL bool owner_bytes_equal(ByteSlice a, ByteSlice b)
{
    return a.pointer && b.pointer && a.length && a.length == b.length && memcmp(a.pointer, b.pointer, a.length) == 0;
}
BUSTER_GLOBAL_LOCAL bool owner_check(bool condition, const char* name)
{
    if (!condition)
    {
        printf("OWNER_COMPILER_CHECK status=fail check=%s\n", name);
    }
    return condition;
}

BUSTER_GLOBAL_LOCAL ProcessResult arena_owner_compiler_probe(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(8), .flags = {.no_pool = 1}});
    bool success = owner_check(arena != 0, "result-arena");
    u32 invocations = 0;
    if (arena)
    {
        compiler_parallel_prewarm();
        String8 paths[] = {S8("owner-main.c"), S8("owner-other.c"), S8("owner-skew.c"), S8("owner-empty.c"), S8("owner-tail.c")};
        String8 first = S8("int other(void); int main(void) { return other() != 42; }\n");
        String8 second = S8("#warning worker-warning\nint other(void) { return 42; }\n");
        success = owner_check(file_write(paths[0], BUSTER_SLICE_TO_BYTE_SLICE(first)), "write-main") && success;
        success = owner_check(file_write(paths[1], BUSTER_SLICE_TO_BYTE_SLICE(second)), "write-other") && success;
        String8 functions[128];
        for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(functions); i += 1)
        {
            functions[i] = string_format(arena, S8("int skew_{u32}(int x) {{ return (x + {u32}) * 3; }}\n"), i, i);
        }
        String8 skew = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(functions), false);
        success = owner_check(file_write(paths[2], BUSTER_SLICE_TO_BYTE_SLICE(skew)), "write-skew") && success;
        success = owner_check(file_write(paths[3], BUSTER_SLICE_TO_BYTE_SLICE(S8("/* empty */\n"))), "write-empty") && success;
        success = owner_check(file_write(paths[4], BUSTER_SLICE_TO_BYTE_SLICE(S8("#warning odd-tail\n"))), "write-tail") && success;
        String8 bad_paths[] = {S8("owner-bad.c"), S8("owner-later.c")};
        success = owner_check(file_write(bad_paths[0], BUSTER_SLICE_TO_BYTE_SLICE(S8("#warning first-warning\nint broken(void) { return missing_value; }\n"))), "write-bad") && success;
        success = owner_check(file_write(bad_paths[1], BUSTER_SLICE_TO_BYTE_SLICE(S8("#warning later-warning\n#error later-error\n"))), "write-later") && success;
        String8 targets[] = {S8("x86_64-unknown-linux"), S8("aarch64-unknown-linux")};
        String8 output = S8("owner-compiler.out");
        CompilerDriverResult baseline[2][2];
        ByteSlice baseline_bytes[2][2];
        memset(baseline, 0, sizeof(baseline));
        memset(baseline_bytes, 0, sizeof(baseline_bytes));
        // Separate serial references for each legal target and input order.
        // Cross-order output identity is not assumed: linker order is observable.
        for (u32 target = 0; target < 2; target += 1)
        {
            for (u32 order = 0; order < 2; order += 1)
            {
                String8 command[] = {S8("-target"), targets[target], S8("-fregister-allocator=fast"), S8("-g"), S8("-nostdinc"),
                                     S8("-o"), output, paths[0], paths[1], paths[2], paths[3], paths[4]};
                if (order)
                {
                    command[7] = paths[2];
                    command[9] = paths[0];
                }
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                invocation.compile_jobs = 1;
                baseline[target][order] = compiler_driver_execute_invocation(arena, invocation);
                invocations += 1;
                success = owner_check(baseline[target][order].error == COMPILER_DRIVER_ERROR_NONE, "serial-reference") && success;
                baseline_bytes[target][order] = file_read(arena, output, (FileReadOptions){0});
            }
        }
        // Comparator negative control: invisible original-position differences
        // and backend-only differences must be detected without rendering.
        CompilerDiagnostic a = {.message = S8("same")};
        CompilerDiagnostic b = a;
        b.primary.original_position.offset = 1;
        success = owner_check(!owner_diagnostic_equal(a, b), "original-offset-control") && success;
        CompilerDiagnosticBackend backend_a = {0};
        CompilerDiagnosticBackend backend_b = {.instruction_id = 1};
        a.backend = &backend_a;
        b = a;
        b.backend = &backend_b;
        success = owner_check(!owner_diagnostic_equal(a, b), "backend-control") && success;
        u32 jobs[] = {2, 1, 2, 2};
        for (u32 attempt = 0; attempt < BUSTER_ARRAY_LENGTH(jobs); attempt += 1)
        {
            u32 target = attempt % 2;
            u32 order = (attempt / 2) % 2;
            String8 failure_command[] = {S8("-target"), targets[target], S8("-nostdinc"), S8("-o"), output, bad_paths[0], bad_paths[1]};
            CompilerDriverInvocation failure = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(failure_command));
            failure.compile_jobs = 1;
            CompilerDriverResult serial_error = compiler_driver_execute_invocation(arena, failure);
            invocations += 1;
            String8 sentinel = S8("failed compilation must preserve existing output");
            success = owner_check(file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)), "write-sentinel") && success;
            failure.compile_jobs = jobs[attempt];
            CompilerDriverResult failed = compiler_driver_execute_invocation(arena, failure);
            invocations += 1;
            u32 active = BUSTER_SINGLE_THREADED ? 1 : jobs[attempt];
            success = owner_check(failed.error == COMPILER_DRIVER_ERROR_ANALYSIS && failed.compilation_workers == active, "failure-and-active-lanes") && success;
            success = owner_check(owner_result_equal(serial_error, failed), "ordered-full-error-records") && success;
            ByteSlice after_failure = file_read(arena, output, (FileReadOptions){0});
            success = owner_check(owner_bytes_equal(after_failure, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)), "output-sentinel") && success;
            String8 command[] = {S8("-target"), targets[target], S8("-fregister-allocator=fast"), S8("-g"), S8("-nostdinc"),
                                 S8("-o"), output, paths[0], paths[1], paths[2], paths[3], paths[4]};
            if (order)
            {
                command[7] = paths[2];
                command[9] = paths[0];
            }
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            invocation.compile_jobs = jobs[attempt];
            CompilerDriverResult recovered = compiler_driver_execute_invocation(arena, invocation);
            invocations += 1;
            ByteSlice recovered_bytes = file_read(arena, output, (FileReadOptions){0});
            success = owner_check(recovered.error == COMPILER_DRIVER_ERROR_NONE && recovered.compilation_workers == active, "recovered-and-active-lanes") && success;
            success = owner_check(owner_result_equal(baseline[target][order], recovered), "recovered-full-diagnostics") && success;
            success = owner_check(owner_bytes_equal(baseline_bytes[target][order], recovered_bytes), "recovered-linked-bytes") && success;
            printf("OWNER_COMPILER_SEQUENCE attempt=%u target=%u order=%u requested=%u active=%u valid_after_failure=%u cumulative_ok=%u\n",
                   attempt, target, order, jobs[attempt], recovered.compilation_workers,
                   (u32)(recovered.error == COMPILER_DRIVER_ERROR_NONE), (u32)success);
        }
        for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(paths); i += 1)
        {
            success = owner_check(os_file_delete(paths[i]), "cleanup-valid-input") && success;
        }
        for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(bad_paths); i += 1)
        {
            success = owner_check(os_file_delete(bad_paths[i]), "cleanup-invalid-input") && success;
        }
        success = owner_check(os_file_delete(output), "cleanup-output") && success;
        arena_destroy(arena, 1);
    }
    printf("OWNER_COMPILER_RESULT status=%s invocations=%u timing=none\n", success ? "pass" : "fail", invocations);
    return success ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
