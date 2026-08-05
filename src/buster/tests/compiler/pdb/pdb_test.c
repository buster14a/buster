#include <buster/tests/compiler/pdb/pdb_test.h>

BUSTER_TEST_F_DECL UnitTestResult pdb_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    DwarfFunction functions[] = {
        {
            .name = S8_INITIALIZER("main"),
            .code_offset = 0,
            .code_size = 0x100,
            .file = 0,
            .line = 3,
        },
        {
            .name = S8_INITIALIZER("helper"),
            .code_offset = 0x100,
            .code_size = 0x100,
            .file = 1,
            .line = 9,
        },
    };
    DwarfLineEntry lines[] = {
        {.code_offset = 0, .file = 0, .line = 3, .column = 1},
        {.code_offset = 0x40, .file = 0, .line = 4, .column = 5},
        {.code_offset = 0x100, .file = 1, .line = 9, .column = 1},
        {.code_offset = 0x180, .file = 1, .line = 11, .column = 3},
    };
    String8 files[] = {
        S8_INITIALIZER("main.c"),
        S8_INITIALIZER("helper.h"),
    };
    DebugTypeId model_parameter_types[] = {0};
    DebugType model_types[] = {
        {
            .name = S8("int"),
            .kind = DEBUG_TYPE_BASE,
            .size = 4,
            .alignment = 4,
            .bit_width = 32,
            .is_signed = true,
        },
        {
            .name = S8("pdb_function"),
            .kind = DEBUG_TYPE_FUNCTION,
            .return_type = 0,
            .parameter_types = model_parameter_types,
            .parameter_count = 1,
        },
    };
    DebugLocationRange model_locations[] = {
        {.start = 0, .end = 0x100, .location = {.kind = DEBUG_LOCATION_FRAME, .frame_offset = -8}},
    };
    DebugVariableId model_variable_ids[] = {0};
    DebugVariable model_variables[] = {
        {
            .name = S8("value"),
            .type = 0,
            .declaration = {.path = files[0], .source = 0, .line = 4, .column = 5},
            .locations = model_locations,
            .location_count = BUSTER_ARRAY_LENGTH(model_locations),
            .kind = DEBUG_VARIABLE_PARAMETER,
        },
    };
    DebugScope model_scopes[] = {
        {
            .kind = DEBUG_SCOPE_FUNCTION,
            .start = 0,
            .end = 0x100,
            .variables = model_variable_ids,
            .variable_count = BUSTER_ARRAY_LENGTH(model_variable_ids),
        },
    };
    DebugFunction model_functions[] = {
        {
            .name = S8("main"),
            .declaration = {.path = files[0], .source = 0, .line = 3, .column = 1},
            .type = 1,
            .scope = 0,
            .code_size = 0x100,
        },
        {
            .name = S8("helper"),
            .declaration = {.path = files[1], .source = 1, .line = 9, .column = 1},
            .type = 1,
            .scope = 0,
            .code_offset = 0x100,
            .code_size = 0x100,
        },
    };
    DebugModel model = {
        .source_paths = files,
        .types = model_types,
        .functions = model_functions,
        .scopes = model_scopes,
        .variables = model_variables,
        .source_count = BUSTER_ARRAY_LENGTH(files),
        .type_count = BUSTER_ARRAY_LENGTH(model_types),
        .function_count = BUSTER_ARRAY_LENGTH(model_functions),
        .scope_count = BUSTER_ARRAY_LENGTH(model_scopes),
        .variable_count = BUSTER_ARRAY_LENGTH(model_variables),
        .valid = true,
    };
    CodeviewResult codeview = codeview_build(arguments->arena, (CodeviewInput){
                                                                  .model = &model,
                                                                  .producer = S8("buster"),
                                                                  .file_paths = files,
                                                                  .functions = functions,
                                                                  .lines = lines,
                                                                  .file_count = BUSTER_ARRAY_LENGTH(files),
                                                                  .function_count = BUSTER_ARRAY_LENGTH(functions),
                                                                  .line_count = BUSTER_ARRAY_LENGTH(lines),
                                                                  .machine = CODEVIEW_MACHINE_X64,
                                                              });
    BUSTER_TEST(arguments, codeview.valid);
    if (!codeview.valid)
    {
        return result;
    }
    PdbSection sections[] = {
        {
            .name = S8_INITIALIZER(".text"),
            .virtual_address = 0x1000,
            .virtual_size = 0x200,
            .raw_size = 0x200,
            .raw_offset = 0x400,
            .characteristics = 0x60000020,
        },
    };
    PdbModule module = {
        .name = S8("demo.obj"),
        .codeview_symbols = codeview.symbols,
        .codeview_types = codeview.types,
        .code_size = 0x200,
        .code_section = 1,
    };
    PdbInput input = {
        .sections = sections,
        .section_count = BUSTER_ARRAY_LENGTH(sections),
        .age = 1,
        .code_section = 1,
        .code_size = 0x200,
        .machine = 0x8664,
        .modules = &module,
        .module_count = 1,
    };
    for (u32 index = 0; index < 16; index += 1)
    {
        input.guid[index] = (u8)(index + 1);
    }
    PdbResult built = pdb_build(arguments->arena, input);
    BUSTER_TEST(arguments, built.valid);
    if (!built.valid)
    {
        return result;
    }
    BUSTER_TEST(arguments, built.bytes.length % PDB_TEST_BLOCK_SIZE == 0);
    BUSTER_TEST(arguments, memcmp(built.bytes.pointer, "Microsoft C/C++ MSF 7.00\r\n\x1a" "DS", 30) == 0);
    u32 block_size = pdb_read_u32(built.bytes, 32);
    u32 block_count = pdb_read_u32(built.bytes, 40);
    u32 directory_size = pdb_read_u32(built.bytes, 44);
    u32 block_map = pdb_read_u32(built.bytes, 52);
    BUSTER_TEST(arguments, block_size == PDB_TEST_BLOCK_SIZE);
    BUSTER_TEST(arguments, (u64)block_count * PDB_TEST_BLOCK_SIZE == built.bytes.length);
    BUSTER_TEST(arguments, block_map < block_count);
    // Walk the directory the way a reader would and check every stream lands
    // inside the file.
    u32 directory_block = pdb_read_u32(built.bytes, (u64)block_map * PDB_TEST_BLOCK_SIZE);
    BUSTER_TEST(arguments, directory_block < block_count);
    u64 directory_base = (u64)directory_block * PDB_TEST_BLOCK_SIZE;
    BUSTER_TEST(arguments, directory_size >= 4);
    u32 stream_count = pdb_read_u32(built.bytes, directory_base);
    BUSTER_TEST(arguments, stream_count == PDB_TEST_STREAM_COUNT);
    if (stream_count != PDB_TEST_STREAM_COUNT)
    {
        return result;
    }
    u64 block_cursor = directory_base + 4 + (u64)stream_count * 4;
    u32 info_block = 0;
    u32 dbi_block = 0;
    u32 module_block = 0;
    for (u32 index = 0; index < stream_count; index += 1)
    {
        u32 size = pdb_read_u32(built.bytes, directory_base + 4 + (u64)index * 4);
        u32 blocks = (size + PDB_TEST_BLOCK_SIZE - 1) / PDB_TEST_BLOCK_SIZE;
        for (u32 block = 0; block < blocks; block += 1)
        {
            u32 block_index = pdb_read_u32(built.bytes, block_cursor);
            block_cursor += 4;
            BUSTER_TEST(arguments, block_index < block_count);
            if (!block && index == PDB_TEST_STREAM_INFO)
            {
                info_block = block_index;
            }
            if (!block && index == PDB_TEST_STREAM_DBI)
            {
                dbi_block = block_index;
            }
            if (!block && index == PDB_TEST_STREAM_MODULE)
            {
                module_block = block_index;
            }
        }
    }
    // The info stream must carry the identity the image will repeat.
    BUSTER_TEST(arguments, info_block != 0);
    u64 info_base = (u64)info_block * PDB_TEST_BLOCK_SIZE;
    BUSTER_TEST(arguments, pdb_read_u32(built.bytes, info_base) == PDB_TEST_INFO_VERSION_VC70);
    BUSTER_TEST(arguments, pdb_read_u32(built.bytes, info_base + 8) == 1);
    BUSTER_TEST(arguments, memcmp(built.bytes.pointer + info_base + 12, input.guid, sizeof(input.guid)) == 0);
    // The DBI header must point at the module and section header streams.
    BUSTER_TEST(arguments, dbi_block != 0);
    u64 dbi_base = (u64)dbi_block * PDB_TEST_BLOCK_SIZE;
    BUSTER_TEST(arguments, pdb_read_u32(built.bytes, dbi_base) == 0xffffffff);
    BUSTER_TEST(arguments, pdb_read_u32(built.bytes, dbi_base + 4) == PDB_TEST_DBI_VERSION_V70);
    u16 dbi_machine = 0;
    memcpy(&dbi_machine, built.bytes.pointer + dbi_base + 58, sizeof(dbi_machine));
    BUSTER_TEST(arguments, dbi_machine == 0x8664);
    u32 dbi_module_size = pdb_read_u32(built.bytes, dbi_base + 24);
    u32 dbi_contribution_size = pdb_read_u32(built.bytes, dbi_base + 28);
    BUSTER_TEST(arguments, dbi_module_size != 0 && dbi_contribution_size != 0);
    u16 module_stream = 0;
    memcpy(&module_stream, built.bytes.pointer + dbi_base + PDB_TEST_DBI_HEADER_SIZE + 4 + PDB_TEST_SECTION_CONTRIBUTION_SIZE + 2, sizeof(module_stream));
    BUSTER_TEST(arguments, module_stream == PDB_TEST_STREAM_MODULE);
    BUSTER_TEST(arguments, module_block != 0);
    bool found_procedure = false;
    bool found_local = false;
    bool found_frame = false;
    if (module_block)
    {
        u64 module_base = (u64)module_block * PDB_TEST_BLOCK_SIZE;
        u64 module_end = BUSTER_MIN(module_base + PDB_TEST_BLOCK_SIZE, built.bytes.length);
        u64 symbol_offset = module_base + 4;
        while (symbol_offset + 4 <= module_end)
        {
            u16 record_length = 0;
            u16 record_kind = 0;
            memcpy(&record_length, built.bytes.pointer + symbol_offset, sizeof(record_length));
            memcpy(&record_kind, built.bytes.pointer + symbol_offset + 2, sizeof(record_kind));
            if (record_length < 2 || symbol_offset + 2 + record_length > module_end)
            {
                break;
            }
            found_procedure |= record_kind == PDB_TEST_S_GPROC32;
            found_local |= record_kind == PDB_TEST_S_LOCAL;
            found_frame |= record_kind == PDB_TEST_S_DEFRANGE_FRAMEPOINTER_REL;
            symbol_offset += (UINT64_C(2) + record_length + UINT64_C(3)) & ~UINT64_C(3);
        }
    }
    BUSTER_TEST(arguments, found_procedure && found_local && found_frame);
#if !BUSTER_IOS
    // Leave the file on disk where external validators can read it. The iOS
    // app keeps the complete PDB validation above in memory instead.
    String8 pdb_path = buster_test_temporary_path(arguments->arena, S8("buster-pdb"), S8(".pdb"));
    BUSTER_TEST(arguments, file_write(pdb_path, built.bytes));
#endif
    // Invalid input must be rejected rather than producing a broken file.
    PdbInput invalid = input;
    invalid.section_count = 0;
    BUSTER_TEST(arguments, !pdb_build(arguments->arena, invalid).valid);
    return result;
}
