// Native investigation's executable boundary. investigation_test_format reads
// an independently authored schema-1 capture and checks malformed inputs;
// investigation_test_driver checks real C lowering, optional capture, identity,
// and ELF placement. Its paired timings are diagnostics, never acceptance gates.
#include <buster/tests/compiler/codegen/investigation_test.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/byte_writer.h>
#include <buster/lib/compiler/codegen/investigation.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>
#include <buster/lib/time.h>

typedef struct InvestigationTestFixture InvestigationTestFixture;
struct InvestigationTestFixture
{
    ByteSlice bytes;
    u64 row_count_offset;
    u64 mark_count_offset;
    u64 code_length_offset;
    u64 rows_offset;
    u64 marks_offset;
    u64 second_mark_offset;
};

typedef struct InvestigationTestSection InvestigationTestSection;
struct InvestigationTestSection
{
    u64 offset;
    u64 size;
    bool found;
};

BUSTER_GLOBAL_LOCAL void investigation_test_emit_u64(ByteWriter* writer, u64 value)
{
    byte_writer_emit_u32_le(writer, (u32)value);
    byte_writer_emit_u32_le(writer, (u32)(value >> 32));
}

BUSTER_GLOBAL_LOCAL void investigation_test_emit_text(ByteWriter* writer, String8 text)
{
    byte_writer_emit_u32_le(writer, (u32)text.length);
    byte_writer_emit_bytes(writer, text.pointer, text.length);
}

BUSTER_GLOBAL_LOCAL void investigation_test_emit_mark(ByteWriter* writer, u32 row, u32 instruction, bool missing)
{
    // This is the schema contract, deliberately independent of serialize().
    u32 fields[] = {
        row, instruction, IR_OPCODE_CAST,
        missing ? IR_ID_UNDERLYING_INVALID : 0, 12, 4,
        missing ? IR_ID_UNDERLYING_INVALID : 0, 12, missing ? 0u : 5u, missing ? 0u : 12u,
        missing ? IR_ID_UNDERLYING_INVALID : 0, 12, missing ? 0u : 5u, missing ? 0u : 12u,
    };
    for (u32 field = 0; field < BUSTER_ARRAY_LENGTH(fields); field += 1)
    {
        byte_writer_emit_u32_le(writer, fields[field]);
    }
    investigation_test_emit_text(writer, missing ? (String8){0} : S8("fixture.c"));
    investigation_test_emit_text(writer, missing ? (String8){0} : S8("fixture.c"));
}

BUSTER_GLOBAL_LOCAL InvestigationTestFixture investigation_test_fixture(Arena* arena, ByteSlice artifact, bool missing_path)
{
    InvestigationTestFixture result = {0};
    u8* storage = arena_allocate(arena, u8, 2048);
    ByteWriter writer = byte_writer_make(storage, 2048);
    byte_writer_emit_bytes(&writer, "BSTRINV1", 8);
    byte_writer_emit_u32_le(&writer, 1);
    investigation_test_emit_text(&writer, S8("cast_unsigned"));
    investigation_test_emit_text(&writer, S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    investigation_test_emit_text(&writer, S8("3:-g02:-c"));
    investigation_test_emit_text(&writer, S8("fixture.c"));
    investigation_test_emit_text(&writer, missing_path ? (String8){0} : S8("fixture.o"));
    investigation_test_emit_text(&writer, S8(".text"));
    investigation_test_emit_text(&writer, S8("x86_64-linux"));
    char8 digest[SHA256_HEX_CAPACITY];
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, artifact.pointer, artifact.length);
    sha256_finish_hex(&hash, digest);
    for (u32 identity = 0; identity < 3; identity += 1)
    {
        byte_writer_emit_bytes(&writer, digest, 64);
    }
    investigation_test_emit_u64(&writer, 1); // Final artifact file offset.
    investigation_test_emit_u64(&writer, 0); // Offset in the named section.
    investigation_test_emit_u64(&writer, 123); // Diagnostic observation only.
    byte_writer_emit_u32_le(&writer, 9); // Canonical function identity.
    byte_writer_emit_u32_le(&writer, 0); // Native emission code base.
    result.row_count_offset = writer.count;
    byte_writer_emit_u32_le(&writer, 3);
    result.mark_count_offset = writer.count;
    byte_writer_emit_u32_le(&writer, 4);
    byte_writer_emit_u32_le(&writer, CODEGEN_REGISTER_ALLOCATOR_FAST);
    byte_writer_emit_u32_le(&writer, 0); // Opaque captured target enum values.
    byte_writer_emit_u32_le(&writer, 0);
    byte_writer_emit_u32_le(&writer, 0);
    byte_writer_emit_u32_le(&writer, 1); // Canonical passes transformed the IR.
    result.code_length_offset = writer.count;
    byte_writer_emit_u32_le(&writer, 2);
    result.rows_offset = writer.count;
    u32 rows[][3] = {
        {0, 0, MACHINE_OPCODE_SKELETON_NOP},
        {0, 1, MACHINE_X64_CVT_F64_TO_U64},
        {1, 2, MACHINE_OPCODE_SKELETON_RETURN},
    };
    for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(rows); row += 1)
    {
        for (u32 field = 0; field < BUSTER_ARRAY_LENGTH(rows[row]); field += 1)
        {
            byte_writer_emit_u32_le(&writer, rows[row][field]);
        }
    }
    result.marks_offset = writer.count;
    investigation_test_emit_mark(&writer, 1, 17, false);
    result.second_mark_offset = writer.count;
    investigation_test_emit_mark(&writer, 1, 17, false); // Duplicate anchors remain explicit.
    investigation_test_emit_mark(&writer, 1, 18, false); // Different IR identities may share one emission row.
    investigation_test_emit_mark(&writer, 3, 19, true); // Missing provenance at a zero-byte terminal anchor.
    byte_writer_emit_u8(&writer, 0x90);
    byte_writer_emit_u8(&writer, 0xc3);
    byte_writer_emit_u32_le(&writer, 0x31444e45); // END1.
    byte_writer_commit(&writer, &result.bytes);
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice investigation_test_mutate_u32(Arena* arena, ByteSlice bytes, u64 offset, u32 value)
{
    ByteSlice result = {.pointer = arena_allocate(arena, u8, bytes.length), .length = bytes.length};
    memcpy(result.pointer, bytes.pointer, bytes.length);
    for (u32 byte = 0; byte < 4; byte += 1)
    {
        result.pointer[offset + byte] = (u8)(value >> (byte * 8));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult investigation_test_format(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u8 artifact_bytes[] = {0xa5, 0x90, 0xc3, 0x5a};
    ByteSlice artifact = BUSTER_ARRAY_TO_BYTE_SLICE(artifact_bytes);
    InvestigationTestFixture fixture = investigation_test_fixture(arena, artifact, false);
    if (BUSTER_REQUIRE(arguments, fixture.bytes.pointer && fixture.bytes.length))
    {
        InvestigationCapture capture = investigation_read(arena, fixture.bytes);
        if (BUSTER_REQUIRE(arguments, capture.found))
        {
            BUSTER_TEST(arguments, capture.transformed);
            BUSTER_TEST(arguments, capture.function == 9 && capture.row_count == 3 && capture.mark_count == 4);
            BUSTER_TEST(arguments, capture.capture_ns == 123 && capture.file_offset == 1);
            BUSTER_STRING_TEST(arguments, capture.function_name, S8("cast_unsigned"));
            if (BUSTER_REQUIRE(arguments, capture.rows && capture.marks && capture.row_count == 3 && capture.mark_count == 4))
            {
                BUSTER_TEST(arguments, capture.rows[0].offset == 0 && capture.rows[0].end == 0);
                BUSTER_TEST(arguments, capture.marks[0].row == 1 && capture.marks[1].row == 1);
                BUSTER_TEST(arguments, capture.marks[0].instruction == 17 && capture.marks[1].instruction == 17);
                BUSTER_TEST(arguments, capture.marks[2].row == 1 && capture.marks[2].instruction == 18);
                BUSTER_TEST(arguments, capture.marks[3].row == capture.row_count);
                BUSTER_TEST(arguments, capture.marks[3].range.source.value == IR_ID_UNDERLYING_INVALID);
                BUSTER_TEST(arguments, !capture.marks[3].path.length && !capture.marks[3].position.line);
            }
            BUSTER_TEST(arguments, investigation_matches(&capture, artifact, S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")));
            BUSTER_TEST(arguments, !investigation_matches(&capture, artifact, S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")));
            InvestigationCapture anonymous = capture;
            anonymous.revision = (String8){0};
            BUSTER_TEST(arguments, !investigation_matches(&anonymous, artifact, S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")));
            artifact_bytes[0] ^= 1; // Identical function bytes in a different artifact must not match.
            BUSTER_TEST(arguments, !investigation_matches(&capture, artifact, S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")));
            artifact_bytes[0] ^= 1;
            ByteSlice serialized = investigation_serialize(arena, &capture);
            BUSTER_TEST(arguments, serialized.length == fixture.bytes.length);
            if (BUSTER_REQUIRE(arguments, serialized.pointer && serialized.length == fixture.bytes.length))
            {
                BUSTER_TEST(arguments, memcmp(serialized.pointer, fixture.bytes.pointer, fixture.bytes.length) == 0);
            }
        }
        for (u64 length = 0; length < fixture.bytes.length; length += 1)
        {
            InvestigationCapture truncated = investigation_read(arena, (ByteSlice){.pointer = fixture.bytes.pointer, .length = length});
            BUSTER_TEST(arguments, !truncated.found && truncated.diagnostic.length);
        }
        struct
        {
            u64 offset;
            u32 value;
        } malformed[] = {
            {0, 0}, // Magic.
            {8, 2}, // Future schema.
            {12, INVESTIGATION_TEXT_LIMIT + 1},
            {fixture.row_count_offset, INVESTIGATION_ROW_LIMIT + 1},
            {fixture.mark_count_offset, INVESTIGATION_MARK_LIMIT + 1},
            {fixture.code_length_offset, (u32)INVESTIGATION_FUNCTION_BYTE_LIMIT + 1},
            {fixture.rows_offset, 1}, // First interval cannot start past its end.
            {fixture.rows_offset + 12, 1}, // A gap after the zero-byte row.
            {fixture.rows_offset + 24 + 4, 3}, // Final interval exceeds code bytes.
            {fixture.marks_offset, 4}, // IR mark references no MIR row or terminal anchor.
            {fixture.second_mark_offset, 0}, // Marks must remain nondecreasing by MIR row.
            {fixture.bytes.length - 4, 0}, // Terminal schema marker.
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(malformed); index += 1)
        {
            ByteSlice bytes = investigation_test_mutate_u32(arena, fixture.bytes, malformed[index].offset, malformed[index].value);
            InvestigationCapture invalid = investigation_read(arena, bytes);
            BUSTER_TEST(arguments, !invalid.found && invalid.diagnostic.length);
        }
        ByteSlice extra = {.pointer = arena_allocate(arena, u8, fixture.bytes.length + 1), .length = fixture.bytes.length + 1};
        memcpy(extra.pointer, fixture.bytes.pointer, fixture.bytes.length);
        extra.pointer[fixture.bytes.length] = 0;
        InvestigationCapture trailing = investigation_read(arena, extra);
        BUSTER_TEST(arguments, !trailing.found && trailing.diagnostic.length);
        ByteSlice too_large = {.pointer = arena_allocate_zeroed(arena, u8, INVESTIGATION_CAPTURE_BYTE_LIMIT + 1),
                              .length = INVESTIGATION_CAPTURE_BYTE_LIMIT + 1};
        memcpy(too_large.pointer, fixture.bytes.pointer, fixture.bytes.length);
        InvestigationCapture oversized = investigation_read(arena, too_large);
        BUSTER_TEST(arguments, !oversized.found && oversized.diagnostic.length);
        InvestigationTestFixture missing_path = investigation_test_fixture(arena, artifact, true);
        InvestigationCapture missing = investigation_read(arena, missing_path.bytes);
        BUSTER_TEST(arguments, !missing.found && missing.diagnostic.length);
    }
    return result;
}

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL u64 investigation_test_read_le(u8 const* bytes, u32 width)
{
    u64 result = 0;
    for (u32 byte = 0; byte < width; byte += 1)
    {
        result |= (u64)bytes[byte] << (byte * 8);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL InvestigationTestSection investigation_test_elf_section(ByteSlice artifact, String8 name)
{
    InvestigationTestSection result = {0};
    // Read ELF section-table locations independently of the production binder.
    bool valid = artifact.pointer && artifact.length >= 64;
    if (valid)
    {
        valid = artifact.pointer[0] == 0x7f && artifact.pointer[1] == 'E' && artifact.pointer[2] == 'L' &&
                artifact.pointer[3] == 'F' && artifact.pointer[4] == 2 && artifact.pointer[5] == 1;
    }
    if (valid)
    {
        u64 table = investigation_test_read_le(artifact.pointer + 40, 8);
        u64 width = investigation_test_read_le(artifact.pointer + 58, 2);
        u64 count = investigation_test_read_le(artifact.pointer + 60, 2);
        u64 names_index = investigation_test_read_le(artifact.pointer + 62, 2);
        valid = width == 64 && count && names_index < count && table <= artifact.length && count <= (artifact.length - table) / width;
        if (valid)
        {
            u8 const* names_header = artifact.pointer + table + names_index * width;
            u64 names_offset = investigation_test_read_le(names_header + 24, 8);
            u64 names_size = investigation_test_read_le(names_header + 32, 8);
            valid = names_offset <= artifact.length && names_size <= artifact.length - names_offset;
            for (u64 section = 0; valid && section < count && !result.found; section += 1)
            {
                u8 const* header = artifact.pointer + table + section * width;
                u64 name_offset = investigation_test_read_le(header, 4);
                if (name_offset < names_size && name.length < names_size - name_offset &&
                    artifact.pointer[names_offset + name_offset + name.length] == 0 &&
                    memcmp(artifact.pointer + names_offset + name_offset, name.pointer, name.length) == 0)
                {
                    result.offset = investigation_test_read_le(header + 24, 8);
                    result.size = investigation_test_read_le(header + 32, 8);
                    result.found = result.offset <= artifact.length && result.size <= artifact.length - result.offset;
                }
            }
        }
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL bool investigation_test_argument_error(Arena* arena, CompilerDriverInvocation invocation)
{
    bool result = invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT;
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
    {
        CompilerDriverResult execution = compiler_driver_execute_invocation(arena, invocation);
        result = execution.error == COMPILER_DRIVER_ERROR_ARGUMENT;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult investigation_test_driver_arguments(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 source = S8("src/buster/tests/compiler/driver/fixtures/investigation.c");
    String8 output = buster_test_temporary_path(arena, S8("buster-investigation-arguments"), S8(".o"));
    String8 sidecar = buster_test_temporary_path(arena, S8("buster-investigation-arguments"), S8(".capture"));
    String8 capture_option = string_format(arena, S8("-finvestigation={S8}"), sidecar);
    String8 common[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-nostdinc"), S8("-g0"), S8("-c"),
        S8("-finvestigation=unused.capture"), S8("-finvestigation-function=cast_unsigned"), source,
    };
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(common));
    BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_STRING_TEST(arguments, invocation.investigation_path, S8("unused.capture"));
    BUSTER_STRING_TEST(arguments, invocation.investigation_function, S8("cast_unsigned"));
    BUSTER_TEST(arguments, invocation.investigation_configuration.length && invocation.investigation_configuration.length <= INVESTIGATION_TEXT_LIMIT);
    String8 rejected[][2] = {
        {S8("-fregister-allocator=quality"), S8("-c")},
        {S8("-fregister-allocator=none"), S8("-c")},
        {S8("-fregister-allocator=fast"), S8("-S")},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rejected); index += 1)
    {
        String8 command[] = {
            S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-nostdinc"), S8("-g0"),
            rejected[index][0], rejected[index][1], capture_option, S8("-finvestigation-function=cast_unsigned"), source, S8("-o"), output,
        };
        CompilerDriverInvocation invalid = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        BUSTER_TEST(arguments, investigation_test_argument_error(arena, invalid));
    }
    String8 missing_function[] = {S8("-c"), capture_option, source, S8("-o"), output};
    CompilerDriverInvocation missing = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing_function));
    BUSTER_TEST(arguments, investigation_test_argument_error(arena, missing));
    String8 alias_output[] = {
        S8("--target=x86_64-linux"), S8("-c"), S8("-o"), sidecar,
        capture_option, S8("-finvestigation-function=cast_unsigned"), source,
    };
    CompilerDriverInvocation alias = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(alias_output));
    BUSTER_TEST(arguments, investigation_test_argument_error(arena, alias));
    CompilerDriverInvocation missing_configuration = invocation;
    missing_configuration.output_path = output;
    missing_configuration.investigation_path = sidecar;
    missing_configuration.investigation_configuration = (String8){0};
    BUSTER_TEST(arguments, investigation_test_argument_error(arena, missing_configuration));
    String8 library_command[] = {
        S8("--target=x86_64-linux"), S8("-c"), S8("-lfoo"), capture_option,
        S8("-finvestigation-function=cast_unsigned"), source, S8("-o"), output,
    };
    CompilerDriverInvocation library = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(library_command));
    BUSTER_TEST(arguments, investigation_test_argument_error(arena, library));
#if !BUSTER_ANDROID && !BUSTER_IOS
    String8 copied_source = buster_test_temporary_path(arena, S8("buster-investigation-alias"), S8(".c"));
    String8 copied_output = buster_test_temporary_path(arena, S8("buster-investigation-alias"), S8(".o"));
    ByteSlice source_bytes = file_read(arena, source, (FileReadOptions){0});
    if (BUSTER_REQUIRE(arguments, source_bytes.pointer && source_bytes.length && file_write(copied_source, source_bytes)))
    {
        u64 separator = 0;
        for (u64 byte = 0; byte < copied_source.length; byte += 1)
        {
            if (copied_source.pointer[byte] == '/')
            {
                separator = byte;
            }
        }
        String8 parent = {.pointer = copied_source.pointer, .length = separator};
        String8 name = {.pointer = copied_source.pointer + separator + 1, .length = copied_source.length - separator - 1};
        String8 source_aliases[] = {copied_source, string_format(arena, S8("{S8}/./{S8}"), parent, name)};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(source_aliases); index += 1)
        {
            String8 resolved_alias[] = {
                S8("--target=x86_64-linux"), S8("-c"), S8("-o"), copied_output,
                string_format(arena, S8("-finvestigation={S8}"), source_aliases[index]),
                S8("-finvestigation-function=cast_unsigned"), copied_source,
            };
            CompilerDriverInvocation normalized = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(resolved_alias));
            BUSTER_TEST(arguments, investigation_test_argument_error(arena, normalized));
            ByteSlice after = file_read(arena, copied_source, (FileReadOptions){0});
            if (BUSTER_REQUIRE(arguments, after.pointer && after.length == source_bytes.length))
            {
                BUSTER_TEST(arguments, memcmp(after.pointer, source_bytes.pointer, source_bytes.length) == 0);
            }
        }
    }
    BUSTER_TEST(arguments, os_file_delete(copied_source));
    BUSTER_TEST(arguments, os_file_delete(copied_output));
#endif
    BUSTER_TEST(arguments, os_file_delete(output));
    BUSTER_TEST(arguments, os_file_delete(sidecar));
    return result;
}

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL UnitTestResult investigation_test_consumer(UnitTestArguments* arguments, InvestigationCapture capture, ByteSlice artifact, u64 offset)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 sidecar = buster_test_temporary_path(arena, S8("buster-investigation-consumer"), S8(".capture"));
    String8 mismatch = buster_test_temporary_path(arena, S8("buster-investigation-consumer-mismatch"), S8(".o"));
    String8 missing = buster_test_temporary_path(arena, S8("buster-investigation-consumer-missing"), S8(".capture"));
    ByteSlice serialized = investigation_serialize(arena, &capture);
    if (BUSTER_REQUIRE(arguments, serialized.pointer && serialized.length && file_write(sidecar, serialized)))
    {
        String8 command[] = {sidecar, capture.artifact_path};
        BUSTER_TEST(arguments, investigation_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)) == PROCESS_RESULT_SUCCESS);
        String8 offset_command[] = {sidecar, capture.artifact_path, string_format(arena, S8("--offset={u64}"), offset)};
        BUSTER_TEST(arguments, investigation_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(offset_command)) == PROCESS_RESULT_SUCCESS);
        String8 wrong_revision = string_equal(capture.revision, S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"))
                                     ? S8("cccccccccccccccccccccccccccccccccccccccc") : S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        String8 expected_option = string_format(arena, S8("--expect-revision={S8}"), wrong_revision);
        String8 revision_command[] = {sidecar, capture.artifact_path, expected_option};
        BUSTER_TEST(arguments, investigation_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(revision_command)) == PROCESS_RESULT_FAILED);
        ByteSlice changed = {.pointer = arena_allocate(arena, u8, artifact.length), .length = artifact.length};
        memcpy(changed.pointer, artifact.pointer, artifact.length);
        changed.pointer[offset] ^= 1; // Preserve the ELF container; change one captured instruction byte.
        if (BUSTER_REQUIRE(arguments, file_write(mismatch, changed)))
        {
            String8 mismatch_command[] = {sidecar, mismatch};
            BUSTER_TEST(arguments, investigation_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(mismatch_command)) == PROCESS_RESULT_FAILED);
        }
        BUSTER_TEST(arguments, os_file_delete(missing));
        String8 missing_capture[] = {missing, capture.artifact_path};
        BUSTER_TEST(arguments, investigation_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing_capture)) == PROCESS_RESULT_FAILED);
        String8 missing_artifact[] = {sidecar, missing};
        BUSTER_TEST(arguments, investigation_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(missing_artifact)) == PROCESS_RESULT_FAILED);
        capture.revision = (String8){0};
        ByteSlice anonymous = investigation_serialize(arena, &capture);
        if (BUSTER_REQUIRE(arguments, anonymous.pointer && anonymous.length && file_write(sidecar, anonymous)))
        {
            BUSTER_TEST(arguments, investigation_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(revision_command)) == PROCESS_RESULT_FAILED);
        }
    }
    BUSTER_TEST(arguments, os_file_delete(sidecar));
    BUSTER_TEST(arguments, os_file_delete(mismatch));
    BUSTER_TEST(arguments, os_file_delete(missing));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult investigation_test_driver(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 source = S8("src/buster/tests/compiler/driver/fixtures/investigation.c");
    String8 output = buster_test_temporary_path(arena, S8("buster-investigation"), S8(".o"));
    String8 sidecar = buster_test_temporary_path(arena, S8("buster-investigation"), S8(".capture"));
    String8 capture_option = string_format(arena, S8("-finvestigation={S8}"), sidecar);
    ByteSlice source_bytes = file_read(arena, source, (FileReadOptions){0});
    char8 source_sha256[SHA256_HEX_CAPACITY];
    Sha256 source_hash;
    sha256_init(&source_hash);
    sha256_add(&source_hash, source_bytes.pointer, source_bytes.length);
    sha256_finish_hex(&source_hash, source_sha256);
    String8 plain_command[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-nostdinc"), S8("-g0"),
        S8("-fregister-allocator=fast"), S8("-fno-machine-fallback"), S8("-c"), source, S8("-o"), output,
    };
    String8 captured_command[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-nostdinc"), S8("-g0"),
        S8("-fregister-allocator=fast"), S8("-fno-machine-fallback"), S8("-c"), source, S8("-o"), output,
        capture_option, S8("-finvestigation-function=cast_unsigned"),
    };
    CompilerDriverInvocation plain = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(plain_command));
    CompilerDriverInvocation captured = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(captured_command));
    plain_command[4] = S8("-fregister-allocator=mir-stack");
    captured_command[4] = S8("-fregister-allocator=mir-stack");
    CompilerDriverInvocation plain_stack = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(plain_command));
    CompilerDriverInvocation captured_stack = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(captured_command));
    BUSTER_TEST(arguments, !plain.investigation_path.length && !plain.investigation_function.length && !plain.investigation_configuration.length);
    if (BUSTER_REQUIRE(arguments, plain.error == COMPILER_DRIVER_ERROR_NONE && captured.error == COMPILER_DRIVER_ERROR_NONE &&
                                  plain_stack.error == COMPILER_DRIVER_ERROR_NONE && captured_stack.error == COMPILER_DRIVER_ERROR_NONE &&
                                  source_bytes.pointer && source_bytes.length))
    {
        compiler_prewarm();
        // Pair zero warms both paths. Alternate order to expose order effects;
        // no threshold or qualified hardware claim is made from these samples.
        for (u32 pair = 0; pair < 5; pair += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arena);
            ByteSlice artifacts[2] = {0};
            u64 elapsed[2] = {0};
            InvestigationCapture capture = {0};
            u64 capture_bytes = 0;
            for (u32 step = 0; step < 2; step += 1)
            {
                u32 enabled = (step + pair) % 2;
                BUSTER_TEST(arguments, os_file_delete(sidecar));
                TimeDataType begin = timestamp_take();
                CompilerDriverInvocation invocation = pair == 4 ? (enabled ? captured_stack : plain_stack) : (enabled ? captured : plain);
                CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
                TimeDataType end = timestamp_take();
                elapsed[enabled] = timestamp_ns_between(begin, end);
                if (compiled.error != COMPILER_DRIVER_ERROR_NONE)
                {
                    arguments->show(arguments, S8("investigation compile enabled={u32}: {S8}\n"), enabled, compiled.diagnostic);
                }
                BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE);
                BUSTER_TEST(arguments, compiled.fallback_record_count == 0);
                BUSTER_TEST(arguments, compiled.diagnostic_count == 0);
                artifacts[enabled] = file_read(arena, output, (FileReadOptions){0});
                BUSTER_TEST(arguments, artifacts[enabled].pointer && artifacts[enabled].length);
                FileReadResult sidecar_read = file_read_checked(arena, sidecar, (FileReadOptions){0});
                if (enabled)
                {
                    if (BUSTER_REQUIRE(arguments, sidecar_read.bytes.pointer && sidecar_read.bytes.length))
                    {
                        capture_bytes = sidecar_read.bytes.length;
                        capture = investigation_read(arena, sidecar_read.bytes);
                    }
                }
                else
                {
                    BUSTER_TEST(arguments, !sidecar_read.bytes.pointer && !sidecar_read.bytes.length);
                }
            }
            if (BUSTER_REQUIRE(arguments, artifacts[0].pointer && artifacts[1].pointer && artifacts[0].length == artifacts[1].length))
            {
                BUSTER_TEST(arguments, memcmp(artifacts[0].pointer, artifacts[1].pointer, artifacts[0].length) == 0);
            }
            if (BUSTER_REQUIRE(arguments, capture.found))
            {
                BUSTER_STRING_TEST(arguments, capture.function_name, S8("cast_unsigned"));
                BUSTER_STRING_TEST(arguments, capture.input_path, source);
                BUSTER_STRING_TEST(arguments, capture.artifact_path, output);
                BUSTER_STRING_TEST(arguments, capture.section_name, S8(".buster_investigation"));
                BUSTER_STRING_TEST(arguments, capture.configuration, pair == 4 ? captured_stack.investigation_configuration : captured.investigation_configuration);
                BUSTER_STRING_TEST(arguments, capture.revision, investigation_compiler_revision());
                BUSTER_STRING_TEST(arguments, string_from_pointer_length(capture.input_sha256, 64), string_from_pointer_length(source_sha256, 64));
                BUSTER_TEST(arguments, capture.cpu == (u32)plain.target.cpu_arch && capture.os == (u32)plain.target.os);
                BUSTER_TEST(arguments, capture.target.length);
                BUSTER_TEST(arguments, capture.allocator == (u32)(pair == 4 ? CODEGEN_REGISTER_ALLOCATOR_MIR_STACK : CODEGEN_REGISTER_ALLOCATOR_FAST));
                BUSTER_TEST(arguments, investigation_matches(&capture, artifacts[1], capture.revision));
                bool conversion = false;
                u64 conversion_offset = 0;
                if (BUSTER_REQUIRE(arguments, capture.rows && capture.marks && capture.row_count && capture.row_count <= INVESTIGATION_ROW_LIMIT &&
                                              capture.mark_count && capture.mark_count <= INVESTIGATION_MARK_LIMIT))
                {
                    for (u32 mark = 0; mark < capture.mark_count; mark += 1)
                    {
                        InvestigationMark anchor = capture.marks[mark];
                        if (anchor.opcode == IR_OPCODE_CAST && anchor.row < capture.row_count)
                        {
                            // A CAST's selection anchor can precede operand
                            // loads. Equal-row anchors remain ambiguous, so
                            // all share the span to the next greater row.
                            u32 next = mark + 1;
                            while (next < capture.mark_count && capture.marks[next].row <= anchor.row)
                            {
                                next += 1;
                            }
                            u32 end_row = next < capture.mark_count ? capture.marks[next].row : capture.row_count;
                            end_row = BUSTER_MIN(end_row, capture.row_count);
                            for (u32 row = anchor.row; row < end_row; row += 1)
                            {
                                if (capture.rows[row].opcode == MACHINE_X64_CVT_F64_TO_U64)
                                {
                                    conversion = true;
                                    conversion_offset = capture.file_offset + capture.rows[row].offset;
                                    BUSTER_STRING_TEST(arguments, anchor.path, source);
                                    BUSTER_TEST(arguments, anchor.position.line == 11 && anchor.position.column);
                                    BUSTER_TEST(arguments, anchor.instruction != IR_ID_UNDERLYING_INVALID);
                                    BUSTER_TEST(arguments, capture.rows[row].end > capture.rows[row].offset);
                                }
                            }
                        }
                    }
                }
                BUSTER_TEST(arguments, conversion);
                InvestigationTestSection section = investigation_test_elf_section(artifacts[1], capture.section_name);
                if (BUSTER_REQUIRE(arguments, section.found))
                {
                    BUSTER_TEST(arguments, capture.section_offset > 0);
                    BUSTER_TEST(arguments, capture.section_offset <= section.size && capture.code.length <= section.size - capture.section_offset);
                    BUSTER_TEST(arguments, capture.file_offset == section.offset + capture.section_offset);
                    if (BUSTER_REQUIRE(arguments, artifacts[1].pointer && capture.code.pointer && capture.file_offset <= artifacts[1].length &&
                                                  capture.code.length <= artifacts[1].length - capture.file_offset))
                    {
                        BUSTER_TEST(arguments, memcmp(artifacts[1].pointer + capture.file_offset, capture.code.pointer, capture.code.length) == 0);
                    }
                }
                if (pair == 0 && BUSTER_REQUIRE(arguments, conversion && artifacts[1].pointer && conversion_offset < artifacts[1].length))
                {
                    UnitTestResult consumer = investigation_test_consumer(arguments, capture, artifacts[1], conversion_offset);
                    result.test_count += consumer.test_count;
                    result.succeeded_test_count += consumer.succeeded_test_count;
                }
                if (pair && pair < 4)
                {
                    bool capture_slower = elapsed[1] >= elapsed[0];
                    u64 difference = capture_slower ? elapsed[1] - elapsed[0] : elapsed[0] - elapsed[1];
                    arguments->show(arguments, S8("INVESTIGATION_DIAGNOSTIC_V1 pair={u32} capture_first={u32} disabled_ns={u64} enabled_ns={u64} delta_sign={S8} delta_ns={u64} capture_ns={u64} capture_bytes={u64} revision={S8} status=diagnostic-only\n"),
                                    pair, pair % 2, elapsed[0], elapsed[1], capture_slower ? S8("+") : S8("-"), difference,
                                    capture.capture_ns, capture_bytes, capture.revision.length ? capture.revision : S8("unavailable"));
                }
            }
            scratch_end(temporary);
        }
    }
    BUSTER_TEST(arguments, os_file_delete(output));
    BUSTER_TEST(arguments, os_file_delete(sidecar));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult investigation_test_fallback(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 source_path = buster_test_temporary_path(arena, S8("buster-investigation-fallback"), S8(".c"));
    String8 output = buster_test_temporary_path(arena, S8("buster-investigation-fallback"), S8(".o"));
    String8 sidecar = buster_test_temporary_path(arena, S8("buster-investigation-fallback"), S8(".capture"));
    // Seventeen asm operands exceed MIR's sixteen-operand cap, which MIR-only
    // codegen reports as an unsupported-instruction failure. No generated code
    // is executed.
    String8 source = S8("int machine_fallback_inline_asm(int value, double floating)\n"
                        "{\n"
                        "    __asm__ __volatile__(\"\" : :\n"
                        "        \"r\"(value), \"r\"(value), \"r\"(value), \"r\"(value), \"r\"(value),\n"
                        "        \"r\"(value), \"r\"(value), \"r\"(value), \"r\"(value),\n"
                        "        \"x\"(floating), \"x\"(floating), \"x\"(floating), \"x\"(floating),\n"
                        "        \"x\"(floating), \"x\"(floating), \"x\"(floating), \"x\"(floating) : \"memory\");\n"
                        "    return value;\n"
                        "}\n");
    String8 sentinel = S8("retain preexisting capture bytes\n");
    String8 command[] = {
        S8("--target=x86_64-linux"), S8("-march=baseline"), S8("-nostdinc"), S8("-g0"),
        S8("-fregister-allocator=fast"), S8("-fcodegen-fallback-census"), S8("-c"), source_path, S8("-o"), output,
    };
    if (BUSTER_REQUIRE(arguments, file_write(source_path, BUSTER_SLICE_TO_BYTE_SLICE(source)) &&
                                  file_write(sidecar, BUSTER_SLICE_TO_BYTE_SLICE(sentinel))))
    {
        CompilerDriverInvocation plain = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        if (BUSTER_REQUIRE(arguments, plain.error == COMPILER_DRIVER_ERROR_NONE))
        {
            ByteSlice sentinel_output = {.pointer = (u8*)"existing-output", .length = 15};
            if (BUSTER_REQUIRE(arguments, file_write(output, sentinel_output)))
            {
                // MIR-only codegen rejects the unsupported function outright, so
                // capture has no canonical fallback to refuse: the ordinary
                // diagnostic must surface and neither artifact may be touched.
                CompilerDriverResult baseline = compiler_driver_execute_invocation(arena, plain);
                BUSTER_TEST(arguments, baseline.error == COMPILER_DRIVER_ERROR_CODEGEN && !baseline.has_object);
                BUSTER_TEST(arguments, baseline.codegen_statistics.fallback_function_count == 0 && baseline.fallback_record_count == 0);
                CompilerDriverInvocation captured = plain;
                captured.investigation_path = sidecar;
                captured.investigation_function = S8("machine_fallback_inline_asm");
                captured.investigation_configuration = S8("API native x86_64-linux baseline fast -c fallback-census -g0");
                CompilerDriverResult rejected = compiler_driver_execute_invocation(arena, captured);
                BUSTER_TEST(arguments, rejected.error == COMPILER_DRIVER_ERROR_CODEGEN && !rejected.has_object);
                BUSTER_TEST(arguments, string_first_sequence(rejected.diagnostic, S8("reason=opcode")) != BUSTER_STRING_NO_MATCH);
                ByteSlice after = file_read(arena, output, (FileReadOptions){0});
                if (BUSTER_REQUIRE(arguments, after.pointer && after.length == sentinel_output.length))
                {
                    BUSTER_TEST(arguments, memcmp(after.pointer, sentinel_output.pointer, sentinel_output.length) == 0);
                }
                ByteSlice retained = file_read(arena, sidecar, (FileReadOptions){0});
                if (BUSTER_REQUIRE(arguments, retained.pointer && retained.length == sentinel.length))
                {
                    BUSTER_TEST(arguments, memcmp(retained.pointer, sentinel.pointer, sentinel.length) == 0);
                }
            }
        }
    }
    BUSTER_TEST(arguments, os_file_delete(source_path));
    BUSTER_TEST(arguments, os_file_delete(output));
    BUSTER_TEST(arguments, os_file_delete(sidecar));
    return result;
}
#endif

UnitTestResult investigation_test(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, investigation_test_format);
    BUSTER_TEST_FIXTURE(arguments, investigation_test_driver_arguments);
#if !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, investigation_test_driver);
    BUSTER_TEST_FIXTURE(arguments, investigation_test_fallback);
#else
    arguments->show(arguments, S8("INVESTIGATION_DIAGNOSTIC_V1 status=unsupported mobile-object-publication\n"));
#endif
    return result;
}
#endif
