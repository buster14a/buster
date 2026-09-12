// Shared diagnostic contract: detached ownership, rendering, original source
// recovery, and producer adapters. Integration failures use real driver inputs.
#include <buster/tests/compiler/diagnostic_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/os_internal.h>

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_diagnostic_test_compile(Arena* arena, String8 path, bool suppress)
{
    String8 command[] = {S8("-fsyntax-only"), S8("-target"), S8("x86_64-unknown-linux"), path};
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
    invocation.suppress_diagnostic_records = suppress;
    return compiler_driver_execute_invocation(arena, invocation);
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_diagnostic_test_write_failures(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 input = buster_test_temporary_path(arguments->arena, S8("diagnostic-write-input"), S8(".c"));
    String8 output = buster_test_temporary_path(arguments->arena, S8("diagnostic-write-output"), S8(".bin"));
    BUSTER_TEST(arguments, file_write(input, BUSTER_SLICE_TO_BYTE_SLICE(S8("int main(void) { return 0; }\n"))));
    String8 actions[] = {S8("-E"), S8("-S"), S8("-c"), S8("-O0")};
    String8 modes[] = {S8("-fregister-allocator=none"), S8("-fregister-allocator=mir-stack"),
                       S8("-fregister-allocator=fast"), S8("-fregister-allocator=quality")};
    OsFileTestStep failures[] = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}};
    for (u32 action = 0; action < BUSTER_ARRAY_LENGTH(actions); action += 1)
    {
        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            String8 command[] = {actions[action], modes[mode], S8("-target"), S8("x86_64-unknown-linux"), input, S8("-o"), output};
            for (u32 failure = 0; failure < BUSTER_ARRAY_LENGTH(failures); failure += 1)
            {
                TemporalArena scratch = scratch_begin(&arguments->arena, 1);
                CompilerDriverInvocation invocation = compiler_driver_parse_arguments(scratch.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
                os_file_test_begin(output, &failures[failure], 1);
                CompilerDriverResult compiled = compiler_driver_execute_invocation(scratch.arena, invocation);
                BUSTER_TEST(arguments, os_file_test_end() == 1);
                BUSTER_TEST_RAW(arguments, compiled.error == (action == 3 ? COMPILER_DRIVER_ERROR_LINK : COMPILER_DRIVER_ERROR_FILE_WRITE),
                    string_format(scratch.arena, S8("write failure action={u32} mode={u32}: {S8}"), action, mode, compiled.diagnostic));
                BUSTER_TEST(arguments, compiled.diagnostic_count == 1);
                if (compiled.diagnostic_count == 1)
                {
                    BUSTER_STRING_TEST(arguments, compiled.diagnostics[0].code, action == 3 ? S8("link.file-write") : S8("driver.file-write"));
                }
                if (action != 3) BUSTER_TEST(arguments, string_first_sequence(compiled.diagnostic, output) < compiled.diagnostic.length);
                scratch_end(scratch);
            }
        }
    }
    BUSTER_TEST(arguments, os_file_delete(input));
    BUSTER_TEST(arguments, os_file_delete(output));
    return result;
}

UnitTestResult compiler_diagnostic_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, compiler_diagnostic_test_write_failures);
    CompilerDiagnostic copy;
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CompilerDiagnosticBackend backend = {
            .target = string_duplicate_arena(temporary.arena, S8("x86_64-linux"), false),
            .reason = string_duplicate_arena(temporary.arena, S8("specific reason"), false),
            .operation_id = UINT32_MAX,
        };
        CompilerDiagnosticNote note = {
            .message = string_duplicate_arena(temporary.arena, S8("previous declaration"), false),
            .location = {.path = S8("prior.h"), .position = {.line = 3, .column = 2}},
        };
        CompilerDiagnostic original = {
            .code = string_duplicate_arena(temporary.arena, S8("c.redefinition"), false),
            .message = string_duplicate_arena(temporary.arena, S8("duplicate definition"), false),
            .symbol = string_duplicate_arena(temporary.arena, S8("value"), false),
            .primary = {.path = S8("mapped.h"), .original_path = S8("physical.h"), .position = {.line = 90, .column = 7}},
            .notes = &note, .note_count = 1, .backend = &backend,
        };
        copy = compiler_diagnostic_copy(arguments->arena, original);
        BUSTER_TEST(arguments, copy.message.pointer != original.message.pointer && copy.notes != original.notes && copy.backend != original.backend);
        // Poison the producer allocation before releasing it; the copied
        // record must remain usable even if the scratch arena is recycled.
        memset(original.message.pointer, 'x', original.message.length);
        memset(backend.reason.pointer, 'x', backend.reason.length);
        memset(note.message.pointer, 'x', note.message.length);
        scratch_end(temporary);
    }
    BUSTER_STRING_TEST(arguments, compiler_diagnostic_render(arguments->arena, copy),
        S8("mapped.h:90:7: duplicate definition\nprior.h:3:2: note: previous declaration"));
    BUSTER_STRING_TEST(arguments, copy.backend->reason, S8("specific reason"));
    BUSTER_STRING_TEST(arguments, copy.symbol, S8("value"));
    BUSTER_STRING_TEST(arguments, copy.primary.original_path, S8("physical.h"));
    BUSTER_TEST(arguments, copy.backend->operation_id == UINT32_MAX);
    BUSTER_STRING_TEST(arguments, compiler_diagnostic_render(arguments->arena,
        (CompilerDiagnostic){.message = S8("unlocated"), .severity = COMPILER_DIAGNOSTIC_WARNING}), S8("warning: unlocated"));

    IrSourceCheckpoint checkpoint = {.offset = 10, .line = 3, .column = 2};
    u32 checkpoint_offset = 0;
    IrSourceRegion regions[] = {
        {.start = 0, .source = 7, .checkpoints = &checkpoint, .checkpoint_offsets = &checkpoint_offset,
         .checkpoint_count = 1, .line_delta = 100, .origin_plus_one = 3},
        {.start = 20, .kind = IR_SOURCE_REGION_STAMP, .stamp = {.source = 7, .line = 103, .column = 6}, .origin_plus_one = 5},
    };
    IrSourceRegionKey keys[] = {{.start = 0, .source = 7}, {.start = 20, .source = 7}};
    IrSourceMap map = {.regions = regions, .keys = keys, .count = 2};
    IrSourcePosition physical = ir_source_map_original_position(&map, 22);
    BUSTER_TEST(arguments, physical.source == 2 && physical.offset == 14 && physical.line == 3 && physical.column == 6);
    BUSTER_TEST(arguments, ir_source_map_position(&map, 4, 0).line == 103);
    regions[1].origin_plus_one = 21;
    BUSTER_TEST(arguments, ir_source_map_original_position(&map, 22).line == 0);
    regions[0].origin_plus_one = 0;
    BUSTER_TEST(arguments, ir_source_map_original_position(&map, 4).line == 0);
    BUSTER_TEST(arguments, ir_source_map_original_position(0, 0).line == 0);

    String8 header_path = buster_test_temporary_path(arguments->arena, S8("diagnostic-include"), S8(".h"));
    String8 input_path = buster_test_temporary_path(arguments->arena, S8("diagnostic-input"), S8(".c"));
    String8 header = S8("#line 200 \"logical-header.h\"\n#define BAD missing_name\nint mapped_error(void) { return BAD; }\n");
    BUSTER_TEST(arguments, file_write(header_path, BUSTER_SLICE_TO_BYTE_SLICE(header)));
    // Both fixtures share a directory. Windows uses a relative temporary
    // root, so spelling that whole path inside the include would resolve it
    // relative to the input again and duplicate the directory prefix.
    u64 header_name_offset = 0;
    for (u64 index = 0; index < header_path.length; index += 1)
    {
        if (header_path.pointer[index] == '/' || header_path.pointer[index] == '\\') header_name_offset = index + 1;
    }
    String8 header_name = {.pointer = header_path.pointer + header_name_offset, .length = header_path.length - header_name_offset};
    String8 source = string_format(arguments->arena, S8("#warning first warning\n#include \"{S8}\"\n"), header_name);
    BUSTER_TEST(arguments, file_write(input_path, BUSTER_SLICE_TO_BYTE_SLICE(source)));
    CompilerDriverResult included = compiler_diagnostic_test_compile(arguments->arena, input_path, false);
    BUSTER_TEST(arguments, included.error == COMPILER_DRIVER_ERROR_ANALYSIS && included.diagnostic_count == 2);
    if (included.diagnostic_count == 2)
    {
        CompilerDiagnostic warning = included.diagnostics[0];
        CompilerDiagnostic error = included.diagnostics[1];
        BUSTER_STRING_TEST(arguments, warning.code, S8("c.preprocessor-warning"));
        BUSTER_STRING_TEST(arguments, warning.primary.path, input_path);
        BUSTER_STRING_TEST(arguments, error.code, S8("c.undeclared-identifier"));
        BUSTER_STRING_TEST(arguments, error.primary.path, S8("logical-header.h"));
        BUSTER_STRING_TEST(arguments, error.primary.original_path, header_path);
        BUSTER_TEST(arguments, error.primary.has_range && error.primary.range.length == 0);
        BUSTER_TEST(arguments, error.primary.position.line == 201 && error.primary.position.column == 33);
        BUSTER_TEST(arguments, error.primary.original_position.line == 3 && error.primary.original_position.column == 33);
        BUSTER_TEST(arguments, error.primary.original_position.offset < header.length && header.pointer[error.primary.original_position.offset] == 'B');
    }
    CompilerDriverResult suppressed = compiler_diagnostic_test_compile(arguments->arena, input_path, true);
    BUSTER_TEST(arguments, suppressed.error == included.error && suppressed.diagnostic_count == 0 && !suppressed.diagnostics);
    BUSTER_STRING_TEST(arguments, suppressed.diagnostic, included.diagnostic);
    BUSTER_STRING_TEST(arguments, suppressed.warning, included.warning);

    // Each input uses a private arena that is destroyed before publication.
    // Read both records afterwards to exercise the driver's ownership boundary.
    String8 first_path = buster_test_temporary_path(arguments->arena, S8("diagnostic-first"), S8(".c"));
    BUSTER_TEST(arguments, file_write(first_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("#warning earlier input\nint first;\n"))));
    String8 multiple_command[] = {S8("-fsyntax-only"), S8("-target"), S8("x86_64-unknown-linux"), first_path, input_path};
    CompilerDriverResult multiple = compiler_driver_execute_invocation(arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(multiple_command)));
    BUSTER_TEST(arguments, multiple.error == COMPILER_DRIVER_ERROR_ANALYSIS && multiple.diagnostic_count == 3);
    if (multiple.diagnostic_count == 3)
    {
        BUSTER_STRING_TEST(arguments, multiple.diagnostics[0].primary.path, first_path);
        BUSTER_STRING_TEST(arguments, multiple.diagnostics[1].primary.path, input_path);
        BUSTER_STRING_TEST(arguments, multiple.diagnostics[2].primary.original_path, header_path);
        BUSTER_TEST(arguments, string_starts_with_sequence(compiler_diagnostic_render(arguments->arena, multiple.diagnostics[2]), S8("logical-header.h:201:33:")));
    }

    header = S8("#line 700 \"directive-only.h\"\n#warning header warning\n#error header error\n");
    BUSTER_TEST(arguments, file_write(header_path, BUSTER_SLICE_TO_BYTE_SLICE(header)));
    CompilerDriverResult directives = compiler_diagnostic_test_compile(arguments->arena, input_path, false);
    BUSTER_TEST(arguments, directives.error == COMPILER_DRIVER_ERROR_TOKENIZE && directives.diagnostic_count == 3);
    if (directives.diagnostic_count == 3)
    {
        BUSTER_STRING_TEST(arguments, directives.diagnostics[1].primary.path, S8("directive-only.h"));
        BUSTER_STRING_TEST(arguments, directives.diagnostics[2].primary.original_path, header_path);
        BUSTER_TEST(arguments, directives.diagnostics[1].primary.position.line == 700);
        BUSTER_TEST(arguments, directives.diagnostics[2].primary.position.line == 701);
        BUSTER_TEST(arguments, directives.diagnostics[2].primary.original_position.line == 3);
    }

    header = S8("int broken = `;\n");
    BUSTER_TEST(arguments, file_write(header_path, BUSTER_SLICE_TO_BYTE_SLICE(header)));
    CompilerDriverResult lexical = compiler_diagnostic_test_compile(arguments->arena, input_path, false);
    BUSTER_TEST(arguments, lexical.error == COMPILER_DRIVER_ERROR_TOKENIZE && lexical.diagnostic_count == 2);
    if (lexical.diagnostic_count == 2)
    {
        BUSTER_STRING_TEST(arguments, lexical.diagnostics[1].code, S8("c.invalid-character"));
        BUSTER_STRING_TEST(arguments, lexical.diagnostics[1].primary.path, header_path);
        BUSTER_TEST(arguments, lexical.diagnostics[1].primary.position.line == 1 && lexical.diagnostics[1].primary.position.column == 14);
    }

    String8 assembly_path = buster_test_temporary_path(arguments->arena, S8("diagnostic-assembly"), S8(".s"));
    String8 assembly = S8(".text\nnot_a_real_opcode %eax\n");
    BUSTER_TEST(arguments, file_write(assembly_path, BUSTER_SLICE_TO_BYTE_SLICE(assembly)));
    CompilerDriverResult assembled = compiler_diagnostic_test_compile(arguments->arena, assembly_path, false);
    BUSTER_TEST(arguments, assembled.error != COMPILER_DRIVER_ERROR_NONE && assembled.diagnostic_count == 1);
    if (assembled.diagnostic_count == 1)
    {
        CompilerDiagnostic diagnostic = assembled.diagnostics[0];
        BUSTER_STRING_TEST(arguments, diagnostic.code, S8("assembly.unknown-instruction"));
        BUSTER_STRING_TEST(arguments, diagnostic.primary.path, assembly_path);
        BUSTER_TEST(arguments, diagnostic.primary.has_range && diagnostic.primary.range.offset == 6);
        BUSTER_TEST(arguments, diagnostic.primary.position.line == 2 && diagnostic.primary.position.column == 1);
        // The unit assembler currently publishes a one-byte location, even
        // when the instruction encoder diagnosed a longer mnemonic.
        BUSTER_TEST(arguments, diagnostic.primary.range.length == 1);
    }
    // Distinct stems also keep these fixtures distinct on case-insensitive
    // filesystems; changing only .s to .S would reuse the preceding input.
    assembly_path = buster_test_temporary_path(arguments->arena, S8("diagnostic-preprocessed-assembly"), S8(".S"));
    assembly = S8("#line 80 \"logical-assembly.S\"\n.text\nmov $1, %eax\nnot_a_real_opcode %eax\n");
    BUSTER_TEST(arguments, file_write(assembly_path, BUSTER_SLICE_TO_BYTE_SLICE(assembly)));
    CompilerDriverResult preassembled = compiler_diagnostic_test_compile(arguments->arena, assembly_path, false);
    BUSTER_TEST(arguments, preassembled.error != COMPILER_DRIVER_ERROR_NONE && preassembled.diagnostic_count == 1);
    if (preassembled.diagnostic_count == 1)
    {
        CompilerDiagnostic diagnostic = preassembled.diagnostics[0];
        BUSTER_STRING_TEST(arguments, diagnostic.code, S8("assembly.unknown-instruction"));
        BUSTER_STRING_TEST(arguments, diagnostic.primary.path, S8("logical-assembly.S"));
        BUSTER_STRING_TEST(arguments, diagnostic.primary.original_path, assembly_path);
        BUSTER_TEST(arguments, diagnostic.primary.has_range && diagnostic.primary.range.length == 0);
        BUSTER_TEST(arguments, diagnostic.primary.position.line == 82 && diagnostic.primary.original_position.line == 4);
        BUSTER_TEST(arguments, diagnostic.primary.original_position.offset == string_first_sequence(assembly, S8("not_a_real_opcode")));
    }
    // Reproduced against the pre-change CLI: this AArch64 form already
    // provides feature evidence in its message. Preserve it without inferring
    // feature names that the producer does not supply.
    assembly = S8(".text\naese v0.16b, v1.16b\n");
    BUSTER_TEST(arguments, file_write(assembly_path, BUSTER_SLICE_TO_BYTE_SLICE(assembly)));
    String8 feature_command[] = {S8("-fsyntax-only"), S8("-target"), S8("aarch64-unknown-linux"), S8("-mattr=-aes"), assembly_path};
    CompilerDriverResult feature = compiler_driver_execute_invocation(arguments->arena,
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(feature_command)));
    BUSTER_TEST(arguments, feature.error != COMPILER_DRIVER_ERROR_NONE && feature.diagnostic_count == 1);
    if (feature.diagnostic_count == 1)
    {
        BUSTER_STRING_TEST(arguments, feature.diagnostics[0].code, S8("assembly.unsupported-feature"));
        BUSTER_STRING_TEST(arguments, feature.diagnostics[0].message, S8("instruction requires an enabled AArch64 target feature"));
    }
    return result;
}
#endif
