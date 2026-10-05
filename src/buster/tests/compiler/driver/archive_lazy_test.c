// Valid mixed-target archive members, serialized with ordinary GNU/COFF and
// BSD ranlib indexes or no index. These controls exercise the real reader and
// both extraction paths without requiring a foreign-target executable.
// compiler_driver_library_order_* checks the parser's independent operation
// oracle, real archive positions, provider identity, lane boundaries and native
// host-linked controls through the registered lazy-archive fixture.

BUSTER_GLOBAL_LOCAL void compiler_driver_archive_test_integer(u8* bytes, u64 value, u32 width, bool big_endian)
{
    for (u32 index = 0; index < width; index += 1)
    {
        u32 byte = big_endian ? width - index - 1 : index;
        bytes[byte] = (u8)(value >> (index * 8));
    }
}

BUSTER_GLOBAL_LOCAL void compiler_driver_archive_test_header(u8* bytes, String8 name, u64 size)
{
    memset(bytes, ' ', 60);
    memcpy(bytes, name.pointer, name.length);
    u8 number[20];
    u32 length = 0;
    do
    {
        number[length++] = (u8)('0' + size % 10);
        size /= 10;
    } while (size);
    BUSTER_CHECK(length <= 10);
    for (u32 digit = 0; digit < length; digit += 1) bytes[48 + digit] = number[length - digit - 1];
    bytes[58] = '`';
    bytes[59] = '\n';
}

BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_archive_test_bytes(Arena* arena, ObjectFile* objects, u32 count, u32 kind)
{
    bool bsd = kind >= 3;
    u32 width = kind == 2 || kind == 4 ? 8 : 4;
    ObjectArtifact* artifacts = arena_allocate(arena, ObjectArtifact, count);
    u64 definitions = 0;
    u64 name_bytes = 0;
    u64 size = 8;
    for (u32 member = 0; member < count; member += 1)
    {
        ObjectFile* object = &objects[member];
        ObjectFormat format = object_format_for_target(object->target);
        artifacts[member] = object_write(arena, object, format);
        BUSTER_CHECK(artifacts[member].error == OBJECT_ERROR_NONE);
        size += 60 + artifacts[member].bytes.length + (artifacts[member].bytes.length & 1);
        for (u32 index = 0; index < object->symbol_count; index += 1)
        {
            ObjectSymbol* symbol = &object->symbols[index];
            if (symbol->global && symbol->section != OBJECT_SECTION_UNDEFINED)
            {
                definitions += 1;
                name_bytes += symbol->name.length + 1 + (format == OBJECT_FORMAT_MACH_O64);
            }
        }
    }
    u64 index_size = width + definitions * width + name_bytes;
    u64 prefix_size = 0;
    String8 index_name = kind == 1 ? S8("/") : kind == 2 ? S8("/SYM64/") : kind == 3 ? S8("__.SYMDEF SORTED") : S8("__.SYMDEF_64 SORTED");
    if (bsd)
    {
        index_size += definitions * width + width;
        prefix_size = kind == 3 ? 20 : 24;
    }
    if (kind) size += 60 + prefix_size + index_size + ((prefix_size + index_size) & 1);
    ByteSlice result = {.pointer = arena_allocate(arena, u8, size), .length = size};
    memcpy(result.pointer, "!<arch>\n", 8);
    u64 cursor = 8;
    u8* index_bytes = 0;
    if (kind)
    {
        String8 header_name = bsd ? (kind == 3 ? S8("#1/20") : S8("#1/24")) : index_name;
        compiler_driver_archive_test_header(result.pointer + cursor, header_name, prefix_size + index_size);
        cursor += 60;
        if (prefix_size)
        {
            memset(result.pointer + cursor, 0, prefix_size);
            memcpy(result.pointer + cursor, index_name.pointer, index_name.length);
            cursor += prefix_size;
        }
        index_bytes = result.pointer + cursor;
        memset(index_bytes, 0, index_size);
        compiler_driver_archive_test_integer(index_bytes, bsd ? definitions * width * 2 : definitions, width, !bsd);
        if (bsd) compiler_driver_archive_test_integer(index_bytes + width + definitions * width * 2, name_bytes, width, false);
        cursor += index_size;
        if (cursor & 1) result.pointer[cursor++] = '\n';
    }
    u64 definition = 0;
    u64 names = 0;
    u64 strings = width + definitions * width * (bsd ? 2 : 1) + (bsd ? width : 0);
    for (u32 member = 0; member < count; member += 1)
    {
        String8 name = string_format(arena, S8("member{u32}.o/"), member);
        compiler_driver_archive_test_header(result.pointer + cursor, name, artifacts[member].bytes.length);
        u64 member_offset = cursor;
        cursor += 60;
        memcpy(result.pointer + cursor, artifacts[member].bytes.pointer, artifacts[member].bytes.length);
        cursor += artifacts[member].bytes.length;
        if (cursor & 1) result.pointer[cursor++] = '\n';
        ObjectFile* object = &objects[member];
        for (u32 symbol = 0; kind && symbol < object->symbol_count; symbol += 1)
        {
            ObjectSymbol* value = &object->symbols[symbol];
            if (value->global && value->section != OBJECT_SECTION_UNDEFINED)
            {
                u64 record = width + definition * width * (bsd ? 2 : 1);
                if (bsd) compiler_driver_archive_test_integer(index_bytes + record, names, width, false);
                compiler_driver_archive_test_integer(index_bytes + record + (bsd ? width : 0), member_offset, width, !bsd);
                if (object_format_for_target(object->target) == OBJECT_FORMAT_MACH_O64) index_bytes[strings + names++] = '_';
                memcpy(index_bytes + strings + names, value->name.pointer, value->name.length);
                names += value->name.length;
                index_bytes[strings + names++] = 0;
                definition += 1;
            }
        }
    }
    BUSTER_CHECK(cursor == result.length);
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_archive_refusal_elf(Arena* arena, String8 definition, u32 type, u32 instruction, bool relocation)
{
    // Independently specified ELF64 little-endian ET_REL: fixed raw offsets,
    // six section headers and two global symbols. Never call object_write.
    BUSTER_CHECK(definition.length <= 14);
    ByteSlice result = {.pointer = arena_allocate_zeroed(arena, u8, 632), .length = 632};
    u8* bytes = result.pointer;
    memcpy(bytes, "\x7f" "ELF", 4);
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    compiler_driver_archive_test_integer(bytes + 16, 1, 2, false);
    compiler_driver_archive_test_integer(bytes + 18, 183, 2, false);
    compiler_driver_archive_test_integer(bytes + 20, 1, 4, false);
    compiler_driver_archive_test_integer(bytes + 40, 248, 8, false);
    compiler_driver_archive_test_integer(bytes + 52, 64, 2, false);
    compiler_driver_archive_test_integer(bytes + 58, 64, 2, false);
    compiler_driver_archive_test_integer(bytes + 60, 6, 2, false);
    compiler_driver_archive_test_integer(bytes + 62, 5, 2, false);
    compiler_driver_archive_test_integer(bytes + 64, instruction, 4, false);
    compiler_driver_archive_test_integer(bytes + 68, UINT32_C(0xd65f03c0), 4, false);
    compiler_driver_archive_test_integer(bytes + 88, ((u64)2 << 32) | type, 8, false);
    compiler_driver_archive_test_integer(bytes + 128, 1, 4, false);
    bytes[132] = 0x12;
    compiler_driver_archive_test_integer(bytes + 134, 1, 2, false);
    compiler_driver_archive_test_integer(bytes + 144, 16, 8, false);
    compiler_driver_archive_test_integer(bytes + 152, definition.length + 2, 4, false);
    bytes[156] = 0x10;
    memcpy(bytes + 177, definition.pointer, definition.length);
    memcpy(bytes + 178 + definition.length, "target", 6);
    String8 names = S8("\0.text\0.rela.text\0.symtab\0.strtab\0.shstrtab\0");
    memcpy(bytes + 200, names.pointer, names.length);
    struct
    {
        u32 name;
        u32 type;
        u64 flags;
        u64 offset;
        u64 size;
        u32 link;
        u32 info;
        u64 alignment;
        u64 entry_size;
    } sections[] = {
        {1, 1, 6, 64, 16, 0, 0, 4, 0},
        {7, 4, 0, 80, relocation ? 24 : 0, 3, 1, 8, 24},
        {18, 2, 0, 104, 72, 4, 1, 8, 24},
        {26, 3, 0, 176, definition.length + 9, 0, 0, 1, 0},
        {34, 3, 0, 200, names.length, 0, 0, 1, 0},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sections); index += 1)
    {
        u8* header = bytes + 248 + (u64)(index + 1) * 64;
        compiler_driver_archive_test_integer(header, sections[index].name, 4, false);
        compiler_driver_archive_test_integer(header + 4, sections[index].type, 4, false);
        compiler_driver_archive_test_integer(header + 8, sections[index].flags, 8, false);
        compiler_driver_archive_test_integer(header + 24, sections[index].offset, 8, false);
        compiler_driver_archive_test_integer(header + 32, sections[index].size, 8, false);
        compiler_driver_archive_test_integer(header + 40, sections[index].link, 4, false);
        compiler_driver_archive_test_integer(header + 44, sections[index].info, 4, false);
        compiler_driver_archive_test_integer(header + 48, sections[index].alignment, 8, false);
        compiler_driver_archive_test_integer(header + 56, sections[index].entry_size, 8, false);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_archive_refusal_bytes(Arena* arena, u32 type, u32 instruction, u32 count, bool gnu_index)
{
    BUSTER_CHECK(count >= 2 && count <= 9);
    ByteSlice members[9] = {0};
    String8 definitions[9] = {S8("safe"), S8("entry")};
    u64 names_size = 0;
    u64 size = 8;
    for (u32 member = 0; member < count; member += 1)
    {
        if (member >= 2) definitions[member] = string_format(arena, S8("unused{u32}"), member);
        members[member] = compiler_driver_archive_refusal_elf(arena, definitions[member], type, instruction, member == 1);
        names_size += definitions[member].length + 1;
        size += 60 + members[member].length;
    }
    u64 index_size = 4 + (u64)count * 4 + names_size;
    if (gnu_index) size += 60 + index_size + (index_size & 1);
    ByteSlice result = {.pointer = arena_allocate_zeroed(arena, u8, size), .length = size};
    memcpy(result.pointer, "!<arch>\n", 8);
    u64 cursor = 8;
    u8* index = 0;
    if (gnu_index)
    {
        compiler_driver_archive_test_header(result.pointer + cursor, S8("/"), index_size);
        index = result.pointer + cursor + 60;
        compiler_driver_archive_test_integer(index, count, 4, true);
        cursor += 60 + index_size;
        if (cursor & 1) result.pointer[cursor++] = '\n';
    }
    u64 name_cursor = 4 + (u64)count * 4;
    for (u32 member = 0; member < count; member += 1)
    {
        if (index)
        {
            compiler_driver_archive_test_integer(index + 4 + (u64)member * 4, cursor, 4, true);
            memcpy(index + name_cursor, definitions[member].pointer, definitions[member].length);
            name_cursor += definitions[member].length + 1;
        }
        compiler_driver_archive_test_header(result.pointer + cursor, string_format(arena, S8("member{u32}.o/"), member), members[member].length);
        memcpy(result.pointer + cursor + 60, members[member].pointer, members[member].length);
        cursor += 60 + members[member].length;
    }
    BUSTER_CHECK(cursor == result.length);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_refusal_exists(String8 path)
{
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = true}, (OpenPermissions){0});
    bool result = file != 0;
    if (file) os_file_close(file);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_refusal_record(CompilerDriverResult* result)
{
    bool valid = result->diagnostic_count == 1 && result->diagnostics;
    if (valid)
    {
        CompilerDiagnostic* record = result->diagnostics;
        valid = string_equal(record->code, S8("driver.object")) && string_equal(record->message, result->diagnostic) &&
                record->severity == COMPILER_DIAGNOSTIC_ERROR && !record->primary.has_range &&
                record->primary.range.source.value == IR_ID_UNDERLYING_INVALID && !record->primary.position.line &&
                !record->primary.original_position.line;
    }
    return valid;
}

#if BUSTER_LINUX && !BUSTER_ANDROID && BUSTER_CPU_ARCH_AARCH64
BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_refusal_host(Arena* arena, String8 source, String8 output)
{
    String8 command[8] = {S8(BUSTER_HOST_C_COMPILER)};
    u32 count = 1;
    String8 first_argument = S8(BUSTER_HOST_C_COMPILER_ARG1);
    if (first_argument.length) command[count++] = first_argument;
    command[count++] = S8("-c");
    command[count++] = source;
    command[count++] = S8("-o");
    command[count++] = output;
    ProcessSpawnResult spawned = os_process_spawn((SliceString8){command, count}, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true, .search_path = true,
                              .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
    bool result = spawned.handle != 0;
    if (result)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
        result = !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS;
    }
    return result;
}
#endif


BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_aarch64_refusal_diagnostics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    Target target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX};
    // Literal ABI names and words are independent of the reader's lookup.
    struct
    {
        u32 type;
        u32 instruction;
        String8 name;
    } rows[] = {
        {279, UINT32_C(0x36000000), S8("R_AARCH64_TSTBR14")},
        {280, UINT32_C(0x34000000), S8("R_AARCH64_CONDBR19")},
        {560, UINT32_C(0x58000000), S8("R_AARCH64_TLSDESC_LD_PREL19")},
        {561, UINT32_C(0x10000000), S8("R_AARCH64_TLSDESC_ADR_PREL21")},
        {562, UINT32_C(0x90000000), S8("R_AARCH64_TLSDESC_ADR_PAGE21")},
        {563, UINT32_C(0xf9400000), S8("R_AARCH64_TLSDESC_LD64_LO12")},
        {564, UINT32_C(0x91000000), S8("R_AARCH64_TLSDESC_ADD_LO12")},
        {565, UINT32_C(0xd2a00000), S8("R_AARCH64_TLSDESC_OFF_G1")},
        {566, UINT32_C(0xf2800000), S8("R_AARCH64_TLSDESC_OFF_G0_NC")},
        {567, UINT32_C(0xf9400000), S8("R_AARCH64_TLSDESC_LDR")},
        {568, UINT32_C(0x91000000), S8("R_AARCH64_TLSDESC_ADD")},
        {569, UINT32_C(0xd63f0000), S8("R_AARCH64_TLSDESC_CALL")},
        {1031, 0, S8("R_AARCH64_TLSDESC")},
        {UINT32_C(0xf0000001), UINT32_C(0xd503201f), {0}},
    };
    for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(rows); row += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arena);
        ByteSlice bytes = compiler_driver_archive_refusal_elf(arena, S8("entry"), rows[row].type, rows[row].instruction, true);
        ObjectFile object = object_read(arena, bytes, target);
        String8 expected = rows[row].name.length
            ? string_format(arena, S8("unsupported ELF AArch64 relocation {S8} (type {u32})"), rows[row].name, rows[row].type)
            : S8("unsupported ELF AArch64 relocation type 4026531841");
        BUSTER_TEST(arguments, object.error == OBJECT_ERROR_UNSUPPORTED_TARGET);
        BUSTER_STRING_TEST(arguments, object.diagnostic, expected);
        // Exercise GNU-indexed and unindexed archives through both extraction
        // algorithms. Member zero is valid; member one is the rejected entry.
        for (u32 gnu_index = 0; gnu_index < 2; gnu_index += 1)
        {
            for (u32 indexed = 0; indexed < 2; indexed += 1)
            {
                u32 count = indexed ? 9 : 2;
                ByteSlice archive_bytes = compiler_driver_archive_refusal_bytes(arena, rows[row].type, rows[row].instruction, count, gnu_index != 0);
                ObjectArchive eager = object_archive_read(arena, archive_bytes, target);
                BUSTER_TEST(arguments, eager.error == OBJECT_ERROR_UNSUPPORTED_TARGET && eager.object_count == 1);
                // Eager diagnostic propagation is owned by #2326; this
                // branch preserves its independent reader-core repair.
                for (u32 selected_bad = 0; selected_bad < 2; selected_bad += 1)
                {
                    ObjectArchive archive = object_archive_read_link(arena, archive_bytes, target);
                    if (BUSTER_REQUIRE(arguments, archive.error == OBJECT_ERROR_NONE && archive.object_count == count))
                    {
                        ObjectSymbol request = {.name = selected_bad ? S8("entry") : S8("safe"),
                            .section = OBJECT_SECTION_UNDEFINED, .kind = OBJECT_SYMBOL_FUNCTION, .global = true};
                        ObjectFile selected[10] = {compiler_driver_archive_test_object(arena, target, &request, 1, 0)};
                        u32 selected_count = 1;
                        CompilerDriverArchiveState state = {0};
                        if (indexed) state.arena = arena_create((ArenaCreation){.flags = {.no_pool = true}});
                        compiler_driver_archive_extract(arena, &state, &archive, selected, &selected_count);
                        BUSTER_TEST(arguments, (state.arena != 0) == (indexed != 0));
                        if (selected_bad)
                        {
                            BUSTER_TEST(arguments, archive.error == OBJECT_ERROR_UNSUPPORTED_TARGET && archive.failed_member == 1 &&
                                                   selected_count == 1 && archive.member_bytes[1].pointer != 0);
                            BUSTER_TEST(arguments, string_first_sequence(archive.diagnostic, expected) != BUSTER_STRING_NO_MATCH &&
                                                   string_first_sequence(archive.diagnostic, S8("selected member member1.o")) != BUSTER_STRING_NO_MATCH);
                        }
                        else
                        {
                            BUSTER_TEST(arguments, archive.error == OBJECT_ERROR_NONE && selected_count == 2 &&
                                                   !archive.member_bytes[0].pointer && archive.member_bytes[1].pointer != 0);
                            BUSTER_STRING_TEST(arguments, selected[1].symbols[0].name, S8("safe"));
                        }
                        if (state.arena) arena_destroy(state.arena, 1);
                    }
                }
            }
        }
        scratch_end(temporary);
    }
    struct
    {
        u32 type;
        u32 instruction;
        ObjectRelocationKind kind;
    } supported[] = {
        {311, UINT32_C(0x90000000), OBJECT_RELOCATION_AARCH64_ELF_GOT_PAGE21},
        {312, UINT32_C(0xf9400000), OBJECT_RELOCATION_AARCH64_ELF_GOT_LD64_LO12},
    };
    for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(supported); row += 1)
    {
        ByteSlice bytes = compiler_driver_archive_refusal_elf(arena, S8("entry"), supported[row].type, supported[row].instruction, true);
        ObjectFile object = object_read(arena, bytes, target);
        if (BUSTER_REQUIRE(arguments, object.error == OBJECT_ERROR_NONE && object.relocation_count == 1))
        {
            BUSTER_TEST(arguments, !object.diagnostic.length && object.relocations[0].kind == supported[row].kind &&
                                   object.relocations[0].addend == 0 && object.relocations[0].offset == 0);
        }
    }
    // Preserve the existing x86-64 fallback's exact wording.
    ByteSlice x86 = compiler_driver_archive_refusal_elf(arena, S8("entry"), 25, UINT32_C(0x90909090), true);
    compiler_driver_archive_test_integer(x86.pointer + 18, 62, 2, false);
    ObjectFile x86_object = object_read(arena, x86, (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX});
    BUSTER_TEST(arguments, x86_object.error == OBJECT_ERROR_UNSUPPORTED_TARGET);
    BUSTER_STRING_TEST(arguments, x86_object.diagnostic, S8("unsupported ELF x86-64 relocation type 25"));

    String8 root = buster_test_temporary_path(arena, S8("buster-reloc-refusal"), S8(""));
    OsDirectoryCreateResult created = os_make_directory(root);
    if (BUSTER_REQUIRE(arguments, created.error.v == 0))
    {
        String8 root_objects[2] = {0};
        String8 programs[] = {S8("int main(void) { return 0; }\n"),
                             S8("int entry(void); int main(void) { return entry(); }\n")};
        bool prepared = true;
        for (u32 program = 0; program < BUSTER_ARRAY_LENGTH(programs); program += 1)
        {
            String8 source = string_format_z(arena, S8("{S8}/root{u32}.c"), root, program);
            root_objects[program] = string_format_z(arena, S8("{S8}/root{u32}.o"), root, program);
            prepared &= file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(programs[program]));
            String8 command[] = {S8("-target"), S8("aarch64-unknown-linux"), S8("-g0"), S8("-nostdinc"),
                                 S8("-c"), source, S8("-o"), root_objects[program]};
            CompilerDriverResult compiled = compiler_driver_execute_invocation(arena,
                compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
            prepared &= compiled.error == COMPILER_DRIVER_ERROR_NONE;
            BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
        }
        if (BUSTER_REQUIRE(arguments, prepared))
        {
            for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(rows); row += 1)
            {
                TemporalArena temporary = arena_begin_temporal(arena);
                String8 expected = rows[row].name.length
                    ? string_format(arena, S8("unsupported ELF AArch64 relocation {S8} (type {u32})"), rows[row].name, rows[row].type)
                    : S8("unsupported ELF AArch64 relocation type 4026531841");
                for (u32 archive = 0; archive < 2; archive += 1)
                {
                    String8 input = string_format_z(arena, S8("{S8}/input-{u32}-{u32}.{S8}"), root, row, archive, archive ? S8("a") : S8("o"));
                    String8 output = string_format_z(arena, S8("{S8}/refused-{u32}-{u32}"), root, row, archive);
                    ByteSlice bytes = archive ? compiler_driver_archive_refusal_bytes(arena, rows[row].type, rows[row].instruction, 9, true)
                                              : compiler_driver_archive_refusal_elf(arena, S8("entry"), rows[row].type, rows[row].instruction, true);
                    BUSTER_TEST(arguments, file_write(input, bytes));
                    u8 sentinel[] = {0xca, 0xfe, 0xba, 0xbe};
                    if (archive) BUSTER_TEST(arguments, file_write(output, (ByteSlice)BUSTER_ARRAY_TO_SLICE(sentinel)));
                    String8 command[8] = {S8("-target"), S8("aarch64-unknown-linux"), S8("-g0"), S8("-o"), output};
                    u32 count = 5;
                    if (archive) command[count++] = root_objects[1];
                    command[count++] = input;
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(arena,
                        compiler_driver_parse_arguments(arena, (SliceString8){command, count}));
                    BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_OBJECT && compiled.object_error == OBJECT_ERROR_UNSUPPORTED_TARGET &&
                                           !compiled.native_link.executable.length && compiler_driver_archive_refusal_record(&compiled));
                    BUSTER_TEST(arguments, string_first_sequence(compiled.diagnostic, input) != BUSTER_STRING_NO_MATCH &&
                                           string_first_sequence(compiled.diagnostic, expected) != BUSTER_STRING_NO_MATCH);
                    if (archive)
                    {
                        BUSTER_TEST(arguments, string_first_sequence(compiled.diagnostic, S8("member1.o")) != BUSTER_STRING_NO_MATCH);
                        ByteSlice preserved = file_read(arena, output, (FileReadOptions){0});
                        BUSTER_TEST(arguments, preserved.length == sizeof(sentinel) && !memcmp(preserved.pointer, sentinel, sizeof(sentinel)));
                        String8 allowed_output = string_format_z(arena, S8("{S8}/unused-{u32}"), root, row);
                        String8 allowed_command[] = {S8("-target"), S8("aarch64-unknown-linux"), S8("-g0"), S8("-o"),
                                                    allowed_output, root_objects[0], input};
                        CompilerDriverResult allowed = compiler_driver_execute_invocation(arena,
                            compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(allowed_command)));
                        BUSTER_TEST_RAW(arguments, allowed.error == COMPILER_DRIVER_ERROR_NONE, allowed.diagnostic);
                        BUSTER_TEST(arguments, allowed.native_link.executable.length && !allowed.diagnostic_count &&
                                               compiler_driver_archive_refusal_exists(allowed_output));
                    }
                    else BUSTER_TEST(arguments, !compiler_driver_archive_refusal_exists(output));
                }
                scratch_end(temporary);
            }
        }
#if BUSTER_LINUX && !BUSTER_ANDROID && BUSTER_CPU_ARCH_AARCH64
        // A real native assembler independently produces CONDBR19. No Buster
        // assembler or object writer participates in this producer control.
        String8 source = string_format_z(arena, S8("{S8}/host-branch.s"), root);
        String8 output = string_format_z(arena, S8("{S8}/host-branch.o"), root);
        String8 assembly = S8(".text\n.globl entry\n.type entry,%function\nentry:\ncbz x0,target\nret\n");
        bool produced = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(assembly)) &&
                        compiler_driver_archive_refusal_host(arena, source, output);
        if (BUSTER_REQUIRE(arguments, produced))
        {
            ObjectFile object = object_read(arena, file_read(arena, output, (FileReadOptions){0}), target);
            BUSTER_TEST(arguments, object.error == OBJECT_ERROR_UNSUPPORTED_TARGET);
            BUSTER_STRING_TEST(arguments, object.diagnostic, S8("unsupported ELF AArch64 relocation R_AARCH64_CONDBR19 (type 280)"));
        }
#endif
        BUSTER_TEST(arguments, os_directory_delete(root));
    }
    return result;
}



// CLI ordering is observed through real serialized archives and final links;
// the expected stream and provider bytes below are independent literal oracles.
BUSTER_GLOBAL_LOCAL bool compiler_driver_library_order_exists(String8 path)
{
    OsFileDescriptor* file = os_file_open(path, (OpenFlags){.read = true}, (OpenPermissions){0});
    bool result = file != 0;
    if (file) BUSTER_CHECK(os_file_close(file));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_library_order_marker(ObjectFile* object, u32 expected, bool weak)
{
    u32 matches = 0;
    bool result = true;
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = &object->symbols[index];
        if (string_equal(symbol->name, S8("marker")) && symbol->section != OBJECT_SECTION_UNDEFINED)
        {
            matches += 1;
            result &= symbol->section == OBJECT_SECTION_DATA && symbol->global && symbol->weak == weak && symbol->size == 4;
            if (result && symbol->section < object->section_count)
            {
                ByteSlice data = object->sections[symbol->section].data;
                u32 value = 0;
                result = symbol->value <= data.length && sizeof(value) <= data.length - symbol->value;
                if (result)
                {
                    memcpy(&value, data.pointer + symbol->value, sizeof(value));
                    result = value == expected;
                }
            }
            else result = false;
        }
    }
    result &= matches == 1;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_library_order_no_marker(ObjectFile* object)
{
    bool result = true;
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = &object->symbols[index];
        result &= !string_equal(symbol->name, S8("marker")) || symbol->section == OBJECT_SECTION_UNDEFINED;
    }
    return result;
}


BUSTER_GLOBAL_LOCAL bool compiler_driver_library_order_unresolved(ObjectFile* object, String8 name)
{
    u32 references = 0;
    bool definition = false;
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = &object->symbols[index];
        if (symbol->global && string_equal(symbol->name, name))
        {
            references += symbol->section == OBJECT_SECTION_UNDEFINED && !symbol->weak;
            definition |= symbol->section != OBJECT_SECTION_UNDEFINED;
        }
    }
    bool result = references == 1 && !definition;
    return result;
}

#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
BUSTER_GLOBAL_LOCAL bool compiler_driver_library_order_process(UnitTestArguments* arguments, Arena* arena, SliceString8 command,
                                                              ProcessResult expected, String8 diagnostic)
{
    ProcessSpawnResult spawned = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.use_process_environment = true, .new_process_group = true, .search_path = true,
                              .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
    bool result = spawned.handle != 0;
    if (result)
    {
        ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
        String8 error = BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]);
        result = !waited.timed_out && waited.result == expected &&
                 (!diagnostic.length || (string_first_sequence(error, diagnostic) != BUSTER_STRING_NO_MATCH &&
                                        string_first_sequence(error, S8("undefined")) != BUSTER_STRING_NO_MATCH));
        if (!result) arguments->show(arguments, S8("library-order process {S8}: result {u32}, timeout {u32}: {S8}\n"),
            command.pointer[0], (u32)waited.result, (u32)waited.timed_out, error);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_library_order_host(UnitTestArguments* arguments, Arena* arena, SliceString8 options,
                                                           ProcessResult expected, String8 diagnostic)
{
    String8 command[24] = {S8(BUSTER_HOST_C_COMPILER)};
    u32 count = 1;
    String8 first_argument = S8(BUSTER_HOST_C_COMPILER_ARG1);
    if (first_argument.length) command[count++] = first_argument;
    BUSTER_CHECK(options.length <= BUSTER_ARRAY_LENGTH(command) - count);
    for (u64 index = 0; index < options.length; index += 1) command[count++] = options.pointer[index];
    bool result = compiler_driver_library_order_process(arguments, arena, (SliceString8){command, count}, expected, diagnostic);
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_library_order_arguments(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 command[] = {S8("-x"), S8("c"), S8("first.input"), S8("-l"), S8("foo"),
                         S8("-x"), S8("none"), S8("second.o"), S8("-l:bar.a"), S8("third.c")};
    CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
    CompilerDriverLinkOperation expected[] = {
        {.kind = COMPILER_DRIVER_LINK_OPERATION_FILE, .index = 0},
        {.kind = COMPILER_DRIVER_LINK_OPERATION_LIBRARY, .index = 0},
        {.kind = COMPILER_DRIVER_LINK_OPERATION_FILE, .index = 1},
        {.kind = COMPILER_DRIVER_LINK_OPERATION_LIBRARY, .index = 1},
        {.kind = COMPILER_DRIVER_LINK_OPERATION_FILE, .index = 2},
    };
    if (BUSTER_REQUIRE(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE && parsed.link_operations &&
                                 parsed.link_operation_count == BUSTER_ARRAY_LENGTH(expected)))
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected); index += 1)
        {
            BUSTER_TEST(arguments, parsed.link_operations[index].kind == expected[index].kind &&
                                   parsed.link_operations[index].index == expected[index].index);
        }
        BUSTER_TEST(arguments, parsed.input_count == 3 && parsed.library_count == 2);
        BUSTER_STRING_TEST(arguments, parsed.input_paths[0], S8("first.input"));
        BUSTER_STRING_TEST(arguments, parsed.libraries[0], S8("foo"));
        BUSTER_STRING_TEST(arguments, parsed.libraries[1], S8(":bar.a"));
        BUSTER_TEST(arguments, parsed.input_languages && parsed.input_language_count == 3 &&
                               parsed.input_languages[0] == COMPILER_DRIVER_LANGUAGE_C &&
                               parsed.input_languages[1] == COMPILER_DRIVER_LANGUAGE_AUTOMATIC &&
                               parsed.input_languages[2] == COMPILER_DRIVER_LANGUAGE_AUTOMATIC);
        String8 output = buster_test_temporary_path(arena, S8("buster-invalid-library-order"), S8(".out"));
        BUSTER_TEST(arguments, !compiler_driver_library_order_exists(output));
        // Validate storage, coverage, kind and each independent index stream
        // before even trying to open the deliberately nonexistent inputs.
        for (u32 mutation = 0; mutation < 7; mutation += 1)
        {
            CompilerDriverLinkOperation operations[BUSTER_ARRAY_LENGTH(expected)];
            memcpy(operations, expected, sizeof(operations));
            CompilerDriverInvocation invalid = parsed;
            invalid.output_path = output;
            invalid.link_operations = operations;
            switch (mutation)
            {
                case 0: invalid.link_operations = 0; break;
                case 1: invalid.link_operation_count -= 1; break;
                case 2: operations[0].kind = COMPILER_DRIVER_LINK_OPERATION_COUNT; break;
                case 3: operations[0].kind = (CompilerDriverLinkOperationKind)-1; break;
                case 4: operations[2].index = 0; break;
                case 5: operations[1].index = 1; break;
                case 6: operations[4].index = UINT32_MAX; break;
                default: BUSTER_TODO();
            }
            CompilerDriverResult rejected = compiler_driver_execute_invocation(arena, invalid);
            BUSTER_TEST(arguments, rejected.error == COMPILER_DRIVER_ERROR_ARGUMENT && !rejected.has_object &&
                                   !rejected.native_link.executable.length);
            BUSTER_STRING_TEST(arguments, rejected.diagnostic, S8("link operation stream does not cover inputs and libraries in order"));
            BUSTER_TEST(arguments, !compiler_driver_library_order_exists(output));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_library_order_cases(UnitTestArguments* arguments, Arena* arena,
                                                                      String8 target, String8 root, String8* objects, bool host)
{
    UnitTestResult result = {0};
    String8 foo = string_format_z(arena, S8("{S8}/libfoo.a"), root);
    String8 bar = string_format_z(arena, S8("{S8}/libbar.a"), root);
    String8 weak = string_format_z(arena, S8("{S8}/libweak.a"), root);
    String8 strong = string_format_z(arena, S8("{S8}/libstrong.a"), root);
    String8 tokens[] = {objects[0], foo, bar, objects[3], objects[4], objects[5], objects[6], objects[7],
                        weak, strong, S8("-lfoo"), S8("-lweak"), S8("-lstrong"), objects[9]};
    struct
    {
        u32 inputs[2][5];
        u32 count;
        String8 unresolved;
        u32 marker;
        bool marker_weak;
    } rows[] = {
        // Named and direct archives occupy the same position in every pair.
        {{{0, 10, 2}, {0, 1, 2}}, 3, {0}, 0, false},
        {{{0, 2, 10}, {0, 2, 1}}, 3, S8("bar"), 0, false},
        {{{0, 2, 10, 2}, {0, 2, 1, 2}}, 4, {0}, 0, false},
        {{{3, 10, 4, 2}, {3, 1, 4, 2}}, 4, S8("foo"), 0, false},
        {{{3, 10, 4, 10, 2}, {3, 1, 4, 1, 2}}, 5, {0}, 0, false},
        {{{6, 11, 9}, {6, 8, 9}}, 3, {0}, 11, true},
        {{{7, 12, 8}, {7, 9, 8}}, 3, {0}, 22, false},
        {{{5, 11, 9}, {5, 8, 9}}, 3, {0}, 0, false},
        {{{7, 11, 13}, {7, 8, 13}}, 3, {0}, 22, false},
    };
    ByteSlice forward = {0};
    for (u32 row = 0; row < BUSTER_ARRAY_LENGTH(rows); row += 1)
    {
        ByteSlice named = {0};
        String8 named_diagnostic = {0};
        CompilerDriverError named_error = COMPILER_DRIVER_ERROR_COUNT;
        for (u32 direct = 0; direct < 2; direct += 1)
        {
            String8 output = string_format_z(arena, S8("{S8}/buster-{u32}-{u32}"), root, row, direct);
            String8 command[16] = {S8("-target"), target, S8("-g0"), S8("-nostdinc"), S8("-L"), root, S8("-o"), output};
            u32 count = 8;
            for (u32 index = 0; index < rows[row].count; index += 1) command[count++] = tokens[rows[row].inputs[direct][index]];
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){command, count});
            CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
            bool success = !rows[row].unresolved.length;
            BUSTER_TEST_RAW(arguments, success ? compiled.error == COMPILER_DRIVER_ERROR_NONE
                                    : compiled.has_object && (compiled.error == COMPILER_DRIVER_ERROR_LINK || (!host && compiled.error == COMPILER_DRIVER_ERROR_NONE)),
                            string_format(arena, S8("library-order {S8} row {u32}, direct {u32}: {S8}"), target, row, direct, compiled.diagnostic));
            if (success && BUSTER_REQUIRE(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object))
            {
                BUSTER_TEST(arguments, rows[row].marker ? compiler_driver_library_order_marker(&compiled.object, rows[row].marker, rows[row].marker_weak)
                                                       : compiler_driver_library_order_no_marker(&compiled.object));
                bool extras_absent = true;
                for (u32 symbol = 0; symbol < compiled.object.symbol_count; symbol += 1)
                {
                    String8 name = compiled.object.symbols[symbol].name;
                    extras_absent &= name.length < 15 || memcmp(name.pointer, "library_unused_", 15) != 0;
                }
                BUSTER_TEST(arguments, extras_absent);
                ByteSlice image = file_read(arena, output, (FileReadOptions){0});
                BUSTER_TEST(arguments, image.pointer && image.length != 0);
                if (!direct) named = image;
                else BUSTER_TEST(arguments, image.length && image.length == named.length && !memcmp(image.pointer, named.pointer, image.length));
                if (!row && !direct) forward = image;
            }
            else if (!success)
            {
                if (BUSTER_REQUIRE(arguments, compiled.has_object))
                {
                    BUSTER_TEST(arguments, compiler_driver_library_order_unresolved(&compiled.object, rows[row].unresolved));
                    // A foreign target may lack a libc export table and defer
                    // final dynamic lookup. Close that world explicitly through
                    // the public writer API, without changing driver policy.
                    String8 closed_output = string_format_z(arena, S8("{S8}/closed-{u32}-{u32}"), root, row, direct);
                    NativeExecutableLinkResult closed = link_native_executable(arena, &compiled.object,
                        (NativeExecutableLinkOptions){.output_path = closed_output, .runtime_exports_known = true});
                    BUSTER_TEST(arguments, closed.error == LINK_ERROR_UNRESOLVED_SYMBOL && string_equal(closed.symbol, rows[row].unresolved));
                    BUSTER_TEST(arguments, !closed.executable.length && !compiler_driver_library_order_exists(closed_output));
                }
                if (host || compiled.error == COMPILER_DRIVER_ERROR_LINK)
                {
                    BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_LINK && compiled.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL &&
                                           string_equal(compiled.native_link.symbol, rows[row].unresolved));
                    BUSTER_TEST(arguments, string_first_sequence(compiled.diagnostic, rows[row].unresolved) != BUSTER_STRING_NO_MATCH &&
                                           !compiler_driver_library_order_exists(output));
                }
                else BUSTER_TEST(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE && !compiled.diagnostic.length &&
                                           compiler_driver_library_order_exists(output));
                if (!direct)
                {
                    named_diagnostic = compiled.diagnostic;
                    named_error = compiled.error;
                }
                else
                {
                    BUSTER_STRING_TEST(arguments, named_diagnostic, compiled.diagnostic);
                    BUSTER_TEST(arguments, compiled.error == named_error);
                }
            }
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
            if (host)
            {
                String8 oracle = string_format_z(arena, S8("{S8}/host-{u32}-{u32}"), root, row, direct);
                String8 options[16] = {S8("-no-pie"), S8("-L"), root, S8("-o"), oracle};
                u32 option_count = 5;
                for (u32 index = 0; index < rows[row].count; index += 1) options[option_count++] = tokens[rows[row].inputs[direct][index]];
                bool host_linked = compiler_driver_library_order_host(arguments, arena, (SliceString8){options, option_count},
                    success ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED, rows[row].unresolved);
                BUSTER_TEST(arguments, host_linked);
                if (host_linked && success)
                {
                    String8 host_command[] = {oracle};
                    BUSTER_TEST(arguments, compiler_driver_library_order_process(arguments, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(host_command),
                                                                                 PROCESS_RESULT_SUCCESS, (String8){0}));
                    if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
                    {
                        String8 buster_command[] = {output};
                        BUSTER_TEST(arguments, compiler_driver_library_order_process(arguments, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_command),
                                                                                     PROCESS_RESULT_SUCCESS, (String8){0}));
                    }
                }
                else if (host_linked) BUSTER_TEST(arguments, !compiler_driver_library_order_exists(oracle));
            }
#else
            BUSTER_UNUSED(host);
#endif
        }
    }
    // Zero-count API invocations explicitly keep their old tail-library order.
    String8 legacy_output = string_format_z(arena, S8("{S8}/legacy-tail-refusal"), root);
    String8 legacy_command[] = {S8("-target"), target, S8("-g0"), S8("-L"), root, S8("-o"), legacy_output, objects[0], S8("-lfoo"), bar};
    CompilerDriverInvocation legacy = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(legacy_command));
    legacy.link_operations = 0;
    legacy.link_operation_count = 0;
    CompilerDriverResult rejected = compiler_driver_execute_invocation(arena, legacy);
    if (BUSTER_REQUIRE(arguments, rejected.has_object))
    {
        BUSTER_TEST(arguments, compiler_driver_library_order_unresolved(&rejected.object, S8("bar")));
        String8 closed_output = string_format_z(arena, S8("{S8}/legacy-closed-refusal"), root);
        NativeExecutableLinkResult closed = link_native_executable(arena, &rejected.object,
            (NativeExecutableLinkOptions){.output_path = closed_output, .runtime_exports_known = true});
        BUSTER_TEST(arguments, closed.error == LINK_ERROR_UNRESOLVED_SYMBOL && string_equal(closed.symbol, S8("bar")) &&
                               !closed.executable.length && !compiler_driver_library_order_exists(closed_output));
    }
    BUSTER_TEST(arguments, host ? rejected.error == COMPILER_DRIVER_ERROR_LINK &&
                                 rejected.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL && string_equal(rejected.native_link.symbol, S8("bar")) &&
                                 !compiler_driver_library_order_exists(legacy_output)
                               : rejected.error == COMPILER_DRIVER_ERROR_NONE ||
                                 (rejected.error == COMPILER_DRIVER_ERROR_LINK && rejected.native_link.error == LINK_ERROR_UNRESOLVED_SYMBOL &&
                                  string_equal(rejected.native_link.symbol, S8("bar")) && !compiler_driver_library_order_exists(legacy_output)));
    legacy_output = string_format_z(arena, S8("{S8}/legacy-tail-success"), root);
    String8 legacy_success[] = {S8("-target"), target, S8("-g0"), S8("-L"), root, S8("-o"), legacy_output,
                               objects[0], S8("-lfoo"), S8("-l:libbar.a")};
    legacy = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(legacy_success));
    legacy.link_operations = 0;
    legacy.link_operation_count = 0;
    CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, legacy);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
    if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice image = file_read(arena, legacy_output, (FileReadOptions){0});
        BUSTER_TEST(arguments, image.length && image.length == forward.length && !memcmp(image.pointer, forward.pointer, image.length));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_library_order_cohorts(UnitTestArguments* arguments, Arena* arena, String8 target,
                                                                        String8 root, String8 main_program, String8 bar_program)
{
    UnitTestResult result = {0};
    String8 main = string_format_z(arena, S8("{S8}/main.input"), root);
    String8 bar = string_format_z(arena, S8("{S8}/bar.c"), root);
    String8 bad = string_format_z(arena, S8("{S8}/bad.c"), root);
    String8 last = string_format_z(arena, S8("{S8}/last.c"), root);
    String8 first_source = string_format(arena, S8("#warning before-library\n{S8}"), main_program);
    String8 second_source = string_format(arena, S8("#warning after-library\n{S8}"), bar_program);
    String8 bad_source = S8("#warning after-library\n#warning at-error\n#error ordered-library-stop\n");
    String8 last_source = S8("#warning discarded-library-warning\nint discarded(void) { return 9; }\n");
    bool prepared = file_write(main, BUSTER_SLICE_TO_BYTE_SLICE(first_source)) && file_write(bar, BUSTER_SLICE_TO_BYTE_SLICE(second_source)) &&
                    file_write(bad, BUSTER_SLICE_TO_BYTE_SLICE(bad_source)) && file_write(last, BUSTER_SLICE_TO_BYTE_SLICE(last_source));
    BUSTER_TEST(arguments, prepared);
    u32 requested[] = {1, (u32)buster_test_worker_count(2), (u32)buster_test_worker_count(4)};
    ByteSlice serial = {0};
    String8 serial_warning = {0};
    String8 serial_error = {0};
    String8 serial_error_warning = {0};
    for (u32 repetition = 0; prepared && repetition < BUSTER_ARRAY_LENGTH(requested); repetition += 1)
    {
        for (u32 direct = 0; direct < 2; direct += 1)
        {
            String8 output = string_format_z(arena, S8("{S8}/cohort-{u32}-{u32}"), root, repetition, direct);
            String8 library = direct ? string_format_z(arena, S8("{S8}/libfoo.a"), root) : S8("-lfoo");
            String8 command[] = {S8("-target"), target, S8("-g0"), S8("-nostdinc"), S8("-L"), root, S8("-o"), output,
                                S8("-x"), S8("c"), main, S8("-x"), S8("none"), library, bar};
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            invocation.compile_jobs = requested[repetition];
            BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.input_language_count == (direct ? 3u : 2u) &&
                                   invocation.input_languages && invocation.input_languages[0] == COMPILER_DRIVER_LANGUAGE_C &&
                                   invocation.input_languages[invocation.input_count - 1] == COMPILER_DRIVER_LANGUAGE_AUTOMATIC);
            CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, invocation);
            BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
            BUSTER_TEST(arguments, compiled.compilation_workers == 1 && compiled.tokenizer_warning_count == 2);
            if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
            {
                ByteSlice image = file_read(arena, output, (FileReadOptions){0});
                BUSTER_TEST(arguments, image.length != 0);
                if (!repetition && !direct)
                {
                    serial = image;
                    serial_warning = compiled.warning;
                }
                else
                {
                    BUSTER_TEST(arguments, image.length && image.length == serial.length && !memcmp(image.pointer, serial.pointer, image.length));
                    BUSTER_STRING_TEST(arguments, serial_warning, compiled.warning);
                }
            }
        }
        String8 output = string_format_z(arena, S8("{S8}/failed-cohort-{u32}"), root, repetition);
        String8 command[] = {S8("-target"), target, S8("-g0"), S8("-nostdinc"), S8("-L"), root, S8("-o"), output,
                            S8("-x"), S8("c"), main, S8("-x"), S8("none"), S8("-lfoo"), bad, last};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        invocation.compile_jobs = requested[repetition];
        CompilerDriverResult rejected = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST(arguments, rejected.error == COMPILER_DRIVER_ERROR_TOKENIZE && !compiler_driver_library_order_exists(output));
        BUSTER_TEST(arguments, string_first_sequence(rejected.diagnostic, S8("ordered-library-stop")) != BUSTER_STRING_NO_MATCH &&
                               string_first_sequence(rejected.warning, S8("before-library")) != BUSTER_STRING_NO_MATCH &&
                               string_first_sequence(rejected.warning, S8("after-library")) != BUSTER_STRING_NO_MATCH &&
                               string_first_sequence(rejected.warning, S8("discarded-library-warning")) == BUSTER_STRING_NO_MATCH);
        if (!repetition)
        {
            serial_error = rejected.diagnostic;
            serial_error_warning = rejected.warning;
        }
        else
        {
            BUSTER_STRING_TEST(arguments, serial_error, rejected.diagnostic);
            BUSTER_STRING_TEST(arguments, serial_error_warning, rejected.warning);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_library_order_execution(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 targets[] = {S8("x86_64-unknown-linux"), S8("aarch64-unknown-linux")};
    u32 target_count = BUSTER_ARRAY_LENGTH(targets);
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    target_count += 1;
#endif
    String8 programs[] = {
        S8("int foo(void); int main(void) { return foo() != 42; }\n"),
        S8("int bar(void); int foo(void) { return bar(); }\n"),
        S8("int bar(void) { return 42; }\n"),
        S8("int later(void); int main(void) { return later() != 42; }\n"),
        S8("int foo(void); int later(void) { return foo(); }\n"),
        S8("int choose(void) __attribute__((weak)); int main(void) { return choose ? choose() != 11 : 0; }\n"),
        S8("int choose(void); extern int marker; int main(void) { return choose() != marker || marker != 11; }\n"),
        S8("int choose(void); extern int marker; int main(void) { return choose() != marker || marker != 22; }\n"),
        S8("int marker __attribute__((weak)) = 11; int choose(void) __attribute__((weak)); int choose(void) { return 11; }\n"),
        S8("int marker = 22; int choose(void) { return 22; }\n"),
    };
    for (u32 target_index = 0; target_index < target_count; target_index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arena);
        bool host = target_index == BUSTER_ARRAY_LENGTH(targets);
        String8 target = targets[target_index % BUSTER_ARRAY_LENGTH(targets)];
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
#if BUSTER_CPU_ARCH_X86_64
        if (host) target = targets[0];
#else
        if (host) target = targets[1];
#endif
        String8 family = S8(BUSTER_HOST_C_COMPILER_ID);
        bool configured = !host || string_equal(family, S8("GNU")) || string_equal(family, S8("Clang"));
#else
        bool configured = true;
#endif
        String8 root = buster_test_temporary_path(arena, string_format(arena, S8("buster-library-order-{u32}"), target_index), S8(""));
        OsDirectoryCreateResult created = os_make_directory(root);
        bool prepared = configured && created.error.v == 0;
        BUSTER_TEST(arguments, prepared);
        String8 sources[BUSTER_ARRAY_LENGTH(programs)] = {0};
        String8 objects[BUSTER_ARRAY_LENGTH(programs)] = {0};
        ObjectFile compiled_objects[BUSTER_ARRAY_LENGTH(programs)] = {0};
        for (u32 index = 0; prepared && index < BUSTER_ARRAY_LENGTH(programs); index += 1)
        {
            sources[index] = string_format_z(arena, S8("{S8}/input{u32}.c"), root, index);
            objects[index] = string_format_z(arena, S8("{S8}/input{u32}.o"), root, index);
            prepared = file_write(sources[index], BUSTER_SLICE_TO_BYTE_SLICE(programs[index]));
            BUSTER_TEST(arguments, prepared);
            if (prepared)
            {
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
                if (host)
                {
                    String8 options[] = {S8("-g0"), S8("-O0"), S8("-fno-pie"), S8("-c"), sources[index], S8("-o"), objects[index]};
                    prepared = compiler_driver_library_order_host(arguments, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(options),
                                                                  PROCESS_RESULT_SUCCESS, (String8){0});
                    BUSTER_TEST(arguments, prepared);
                }
                else
#endif
                {
                    String8 command[] = {S8("-target"), target, S8("-g0"), S8("-nostdinc"), S8("-c"), sources[index], S8("-o"), objects[index]};
                    CompilerDriverResult compiled = compiler_driver_execute_invocation(arena,
                        compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command)));
                    prepared = compiled.error == COMPILER_DRIVER_ERROR_NONE && compiled.has_object;
                    BUSTER_TEST_RAW(arguments, prepared, compiled.diagnostic);
                    compiled_objects[index] = compiled.object;
                }
            }
        }
        String8 archive_names[] = {S8("foo"), S8("bar"), S8("weak"), S8("strong")};
        u32 member_indices[] = {1, 2, 8, 9};
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
        String8 archiver = host ? executable_resolve_in_path(arena, S8("ar")) : (String8){0};
        if (host) { prepared = prepared && archiver.length != 0; BUSTER_TEST(arguments, prepared); }
#endif
        for (u32 index = 0; prepared && index < BUSTER_ARRAY_LENGTH(archive_names); index += 1)
        {
            String8 archive = string_format_z(arena, S8("{S8}/lib{S8}.a"), root, archive_names[index]);
#if BUSTER_LINUX && !BUSTER_ANDROID && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
            if (host)
            {
                // Independently produced GNU archives exercise the consumer,
                // alongside the target-independent indexed serializer below.
                String8 command[] = {archiver, S8("rcs"), archive, objects[member_indices[index]]};
                prepared = compiler_driver_library_order_process(arguments, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command),
                                                                 PROCESS_RESULT_SUCCESS, (String8){0});
            }
            else
#endif
            {
                // Nine members force the existing indexed state, with only
                // the named provider eligible. The eight extras must stay out.
                ObjectFile members[9] = {0};
                members[0] = compiled_objects[member_indices[index]];
                for (u32 unused = 1; unused < BUSTER_ARRAY_LENGTH(members); unused += 1)
                {
                    ObjectSymbol* symbol = arena_allocate(arena, ObjectSymbol, 1);
                    *symbol = (ObjectSymbol){.name = string_format(arena, S8("library_unused_{u32}_{u32}"), index, unused),
                        .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .size = 1, .global = true};
                    members[unused] = compiler_driver_archive_test_object(arena, compiled_objects[0].target, symbol, 1, 99);
                }
                ByteSlice bytes = compiler_driver_archive_test_bytes(arena, members, BUSTER_ARRAY_LENGTH(members), 1);
                prepared = file_write(archive, bytes);
            }
            BUSTER_TEST(arguments, prepared);
        }
        if (prepared)
        {
            UnitTestResult cases = compiler_driver_library_order_cases(arguments, arena, target, root, objects, host);
            result.test_count += cases.test_count;
            result.succeeded_test_count += cases.succeeded_test_count;
            if (!host)
            {
                UnitTestResult cohorts = compiler_driver_library_order_cohorts(arguments, arena, target, root, programs[0], programs[2]);
                result.test_count += cohorts.test_count;
                result.succeeded_test_count += cohorts.succeeded_test_count;
            }
        }
        if (created.error.v == 0) BUSTER_TEST(arguments, os_directory_delete(root));
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_archive_test_default_link(Arena* arena, String8 target, String8 sysroot,
                                                                                 String8 input, String8 library, String8 explicit_root,
                                                                                 String8 output)
{
    String8 command[10] = {S8("-target"), target, S8("-g0"), string_format(arena, S8("--sysroot={S8}"), sysroot), input};
    u32 count = 5;
    if (explicit_root.length)
    {
        command[count++] = S8("-L");
        command[count++] = explicit_root;
    }
    command[count++] = library;
    command[count++] = S8("-o");
    command[count++] = output;
    return compiler_driver_execute_invocation(arena, compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = count}));
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_default_result(UnitTestArguments* arguments, CompilerDriverResult linked,
                                                                            u8 expected, ByteSlice reference)
{
    UnitTestResult result = {0};
    BUSTER_TEST_RAW(arguments, linked.error == COMPILER_DRIVER_ERROR_NONE, linked.diagnostic);
    if (BUSTER_REQUIRE(arguments, linked.error == COMPILER_DRIVER_ERROR_NONE && linked.has_object))
    {
        bool found = false;
        for (u32 index = 0; index < linked.object.symbol_count; index += 1)
        {
            ObjectSymbol* symbol = linked.object.symbols + index;
            if (string_equal(symbol->name, S8("buster1285_selected")))
            {
                bool defined = symbol->section == OBJECT_SECTION_DATA && symbol->section < linked.object.section_count;
                BUSTER_TEST(arguments, defined);
                if (defined)
                {
                    ByteSlice data = linked.object.sections[symbol->section].data;
                    found = symbol->value < data.length && data.pointer[symbol->value] == expected;
                }
            }
        }
        BUSTER_TEST(arguments, found);
        BUSTER_TEST(arguments, linked.native_link.executable.length != 0);
        if (reference.pointer)
        {
            ByteSlice image = linked.native_link.executable;
            BUSTER_TEST(arguments, image.pointer && image.length == reference.length && memcmp(image.pointer, reference.pointer, image.length) == 0);
        }
    }
    return result;
}

// Literal sysroot paths are independent of the production root builder. Distinct
// provider bytes make search precedence observable before image serialization.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_default_roots(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 targets[] = {S8("x86_64-linux"), S8("aarch64-linux")};
    String8 triples[] = {S8("x86_64-linux-gnu"), S8("aarch64-linux-gnu")};
    for (u32 cpu = 0; cpu < BUSTER_ARRAY_LENGTH(targets); cpu += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        Arena* arena = arguments->arena;
        String8 sysroot = buster_test_temporary_path(arena, S8("buster-default-library-roots"), S8(""));
        os_make_directory(sysroot);
        String8 lib = string_format_z(arena, S8("{S8}/lib"), sysroot);
        String8 usr = string_format_z(arena, S8("{S8}/usr"), sysroot);
        String8 usr_lib = string_format_z(arena, S8("{S8}/usr/lib"), sysroot);
        os_make_directory(lib);
        os_make_directory(usr);
        os_make_directory(usr_lib);
        String8 roots[] = {
            string_format_z(arena, S8("{S8}/lib/{S8}"), sysroot, triples[cpu]),
            string_format_z(arena, S8("{S8}/usr/lib/{S8}"), sysroot, triples[cpu]),
            string_format_z(arena, S8("{S8}/lib64"), sysroot),
            string_format_z(arena, S8("{S8}/usr/lib64"), sysroot), lib, usr_lib,
        };
        for (u32 root = 0; root < BUSTER_ARRAY_LENGTH(roots); root += 1) { os_make_directory(roots[root]); }
        Target target = {.cpu_arch = cpu ? CPU_ARCH_AARCH64 : CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
        ObjectSymbol symbols[] = {
            {.name = S8("main"), .section = OBJECT_SECTION_TEXT, .kind = OBJECT_SYMBOL_FUNCTION, .global = true},
            {.name = S8("buster1285_selected"), .section = OBJECT_SECTION_UNDEFINED, .kind = OBJECT_SYMBOL_DATA, .global = true,
             .thread_local_state = OBJECT_SYMBOL_THREAD_LOCAL_NO},
        };
        ObjectFile caller = compiler_driver_archive_test_object(arena, target, symbols, BUSTER_ARRAY_LENGTH(symbols), 0);
        u8 x86[] = {0x31, 0xc0, 0xc3};
        u8 aarch64[] = {0x00, 0x00, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6};
        caller.sections[OBJECT_SECTION_TEXT].data = cpu ? (ByteSlice)BUSTER_ARRAY_TO_SLICE(aarch64) : (ByteSlice)BUSTER_ARRAY_TO_SLICE(x86);
        ObjectArtifact artifact = object_write(arena, &caller, OBJECT_FORMAT_ELF64);
        String8 input = string_format_z(arena, S8("{S8}/main.o"), sysroot);
        if (BUSTER_REQUIRE(arguments, artifact.error == OBJECT_ERROR_NONE && file_write(input, artifact.bytes)))
        {
            String8 archive_paths[BUSTER_ARRAY_LENGTH(roots)];
            for (u32 root = 0; root < BUSTER_ARRAY_LENGTH(roots); root += 1)
            {
                ObjectSymbol provider = {.name = S8("buster1285_selected"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA,
                                         .global = true, .size = 1};
                ObjectFile member = compiler_driver_archive_test_object(arena, target, &provider, 1, (u8)(root + 11));
                ByteSlice archive = compiler_driver_archive_test_bytes(arena, &member, 1, 1);
                archive_paths[root] = string_format_z(arena, S8("{S8}/libbuster1285_marker.a"), roots[root]);
                if (BUSTER_REQUIRE(arguments, file_write(archive_paths[root], archive)))
                {
                    String8 output = string_format_z(arena, S8("{S8}/linked"), sysroot);
                    CompilerDriverResult direct = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input, archive_paths[root],
                                                                                          (String8){0}, output);
                    UnitTestResult checked = compiler_driver_archive_test_default_result(arguments, direct, (u8)(root + 11), (ByteSlice){0});
                    result.test_count += checked.test_count;
                    result.succeeded_test_count += checked.succeeded_test_count;
                    String8 requests[] = {S8("-lbuster1285_marker"), S8("-l:libbuster1285_marker.a")};
                    for (u32 exact = 0; exact < BUSTER_ARRAY_LENGTH(requests); exact += 1)
                    {
                        CompilerDriverResult named = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input, requests[exact],
                                                                                             (String8){0}, output);
                        checked = compiler_driver_archive_test_default_result(arguments, named, (u8)(root + 11), direct.native_link.executable);
                        result.test_count += checked.test_count;
                        result.succeeded_test_count += checked.succeeded_test_count;
                    }
                    BUSTER_TEST(arguments, os_file_delete(archive_paths[root]));
                }
            }
            // With all default roots populated, the first multiarch lib wins.
            for (u32 root = 0; root < BUSTER_ARRAY_LENGTH(roots); root += 1)
            {
                ObjectSymbol provider = {.name = S8("buster1285_selected"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA,
                                         .global = true, .size = 1};
                ObjectFile member = compiler_driver_archive_test_object(arena, target, &provider, 1, (u8)(root + 31));
                BUSTER_TEST(arguments, file_write(archive_paths[root], compiler_driver_archive_test_bytes(arena, &member, 1, 1)));
            }
            String8 output = string_format_z(arena, S8("{S8}/precedence"), sysroot);
            CompilerDriverResult ordered = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input, S8("-lbuster1285_marker"),
                                                                                     (String8){0}, output);
            UnitTestResult checked = compiler_driver_archive_test_default_result(arguments, ordered, 31, (ByteSlice){0});
            result.test_count += checked.test_count;
            result.succeeded_test_count += checked.succeeded_test_count;
            String8 explicit_root = string_format_z(arena, S8("{S8}/explicit"), sysroot);
            os_make_directory(explicit_root);
            String8 explicit_archive = string_format_z(arena, S8("{S8}/libbuster1285_marker.a"), explicit_root);
            ObjectSymbol provider = {.name = S8("buster1285_selected"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA,
                                     .global = true, .size = 1};
            ObjectFile member = compiler_driver_archive_test_object(arena, target, &provider, 1, 77);
            BUSTER_TEST(arguments, file_write(explicit_archive, compiler_driver_archive_test_bytes(arena, &member, 1, 1)));
            String8 shared = string_format_z(arena, S8("{S8}/libbuster1285_marker.so"), roots[0]);
            // These independent ELF header bytes identify a foreign
            // machine before export discovery. It must not suppress either
            // the archive beside it or a later root's archive (issue 1285).
            u8 alien_header[64] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
            compiler_driver_archive_test_integer(alien_header + 16, 3, 2, false);
            compiler_driver_archive_test_integer(alien_header + 18, cpu ? 62 : 183, 2, false);
            compiler_driver_archive_test_integer(alien_header + 20, 1, 4, false);
            compiler_driver_archive_test_integer(alien_header + 52, 64, 2, false);
            BUSTER_TEST(arguments, file_write(shared, (ByteSlice)BUSTER_ARRAY_TO_SLICE(alien_header)));
            CompilerDriverResult compatible = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input,
                                                                                        S8("-lbuster1285_marker"), (String8){0}, output);
            checked = compiler_driver_archive_test_default_result(arguments, compatible, 31, ordered.native_link.executable);
            result.test_count += checked.test_count;
            result.succeeded_test_count += checked.succeeded_test_count;
            BUSTER_TEST(arguments, os_file_delete(archive_paths[0]));
            CompilerDriverResult later = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input,
                                                                                   S8("-lbuster1285_marker"), (String8){0}, output);
            checked = compiler_driver_archive_test_default_result(arguments, later, 32, (ByteSlice){0});
            result.test_count += checked.test_count;
            result.succeeded_test_count += checked.succeeded_test_count;
            member.sections[OBJECT_SECTION_DATA].data.pointer[0] = 31;
            BUSTER_TEST(arguments, file_write(archive_paths[0], compiler_driver_archive_test_bytes(arena, &member, 1, 1)));
            String8 explicit_shared = string_format_z(arena, S8("{S8}/libbuster1285_marker.so"), explicit_root);
            BUSTER_TEST(arguments, file_write(explicit_shared, (ByteSlice)BUSTER_ARRAY_TO_SLICE(alien_header)));
            CompilerDriverResult explicit_compatible = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input,
                                                                                                 S8("-lbuster1285_marker"), explicit_root, output);
            checked = compiler_driver_archive_test_default_result(arguments, explicit_compatible, 77, (ByteSlice){0});
            result.test_count += checked.test_count;
            result.succeeded_test_count += checked.succeeded_test_count;
            BUSTER_TEST(arguments, os_file_delete(explicit_shared));
            ByteSlice sentinel = BUSTER_SLICE_TO_BYTE_SLICE(S8("existing output"));
            BUSTER_TEST(arguments, file_write(shared, BUSTER_SLICE_TO_BYTE_SLICE(S8("unreadable shared object"))));
            CompilerDriverResult explicit = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input,
                                                                                      S8("-lbuster1285_marker"), explicit_root, output);
            checked = compiler_driver_archive_test_default_result(arguments, explicit, 77, (ByteSlice){0});
            result.test_count += checked.test_count;
            result.succeeded_test_count += checked.succeeded_test_count;
            CompilerDriverResult exact = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input,
                                                                                   S8("-l:libbuster1285_marker.a"), (String8){0}, output);
            checked = compiler_driver_archive_test_default_result(arguments, exact, 31, ordered.native_link.executable);
            result.test_count += checked.test_count;
            result.succeeded_test_count += checked.succeeded_test_count;
            BUSTER_TEST(arguments, file_write(output, sentinel));
            CompilerDriverResult shadowed = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input,
                                                                                      S8("-lbuster1285_marker"), (String8){0}, output);
            BUSTER_TEST(arguments, shadowed.error == COMPILER_DRIVER_ERROR_LINK);
            BUSTER_STRING_TEST(arguments, shadowed.diagnostic, S8("cannot find -lbuster1285_marker"));
            ByteSlice preserved = file_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, preserved.length == sentinel.length && memcmp(preserved.pointer, sentinel.pointer, sentinel.length) == 0);
            BUSTER_TEST(arguments, shadowed.native_link.executable.length == 0);
            // A supplied sysroot must not fall back to the host's ordinary libm.
            CompilerDriverResult isolated = compiler_driver_archive_test_default_link(arena, targets[cpu], sysroot, input, S8("-lm"),
                                                                                      (String8){0}, output);
            BUSTER_TEST(arguments, isolated.error == COMPILER_DRIVER_ERROR_LINK);
            BUSTER_STRING_TEST(arguments, isolated.diagnostic, S8("cannot find -lm"));
            preserved = file_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, preserved.length == sentinel.length && memcmp(preserved.pointer, sentinel.pointer, sentinel.length) == 0);
            BUSTER_TEST(arguments, isolated.native_link.executable.length == 0);
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_default_native(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if defined(BUSTER_HOST_C_COMPILER) && BUSTER_LINUX && !BUSTER_ANDROID
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = arguments->arena;
    String8 sysroot = buster_test_temporary_path(arena, S8("buster-default-library-native"), S8(""));
    os_make_directory(sysroot);
    String8 usr = string_format_z(arena, S8("{S8}/usr"), sysroot);
    String8 root = string_format_z(arena, S8("{S8}/usr/lib"), sysroot);
    os_make_directory(usr);
    os_make_directory(root);
    String8 source = string_format_z(arena, S8("{S8}/main.c"), sysroot);
    String8 provider = string_format_z(arena, S8("{S8}/provider.c"), sysroot);
    String8 member = string_format_z(arena, S8("{S8}/provider.o"), sysroot);
    String8 archive = string_format_z(arena, S8("{S8}/libbuster1285_native.a"), root);
    String8 host_output = string_format_z(arena, S8("{S8}/host"), sysroot);
    String8 named_output = string_format_z(arena, S8("{S8}/named"), sysroot);
    String8 direct_output = string_format_z(arena, S8("{S8}/direct"), sysroot);
    String8 archiver = executable_resolve_in_path(arena, S8("llvm-ar"));
    if (!archiver.length) { archiver = executable_resolve_in_path(arena, S8("ar")); }
    bool written = file_write(source, BUSTER_SLICE_TO_BYTE_SLICE(S8(
        "extern unsigned char buster1285_selected;\nint main(void) { return buster1285_selected != 91; }\n"))) &&
        file_write(provider, BUSTER_SLICE_TO_BYTE_SLICE(S8("unsigned char buster1285_selected = 91;\n")));
    if (BUSTER_REQUIRE(arguments, written && archiver.length))
    {
        String8 command[12];
        u32 count = 0;
        command[count++] = S8(BUSTER_HOST_C_COMPILER);
        if (S8(BUSTER_HOST_C_COMPILER_ARG1).length) { command[count++] = S8(BUSTER_HOST_C_COMPILER_ARG1); }
        command[count++] = S8("-c");
        command[count++] = provider;
        command[count++] = S8("-o");
        command[count++] = member;
        ProcessWaitResult compiled = compiler_driver_test_response_file_run(arena, (SliceString8){.pointer = command, .length = count});
        if (BUSTER_REQUIRE(arguments, !compiled.timed_out && compiled.result == PROCESS_RESULT_SUCCESS))
        {
            String8 archive_command[] = {archiver, S8("rcs"), archive, member};
            ProcessWaitResult archived = compiler_driver_test_response_file_run(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(archive_command));
            if (BUSTER_REQUIRE(arguments, !archived.timed_out && archived.result == PROCESS_RESULT_SUCCESS))
            {
                count = 0;
                command[count++] = S8(BUSTER_HOST_C_COMPILER);
                if (S8(BUSTER_HOST_C_COMPILER_ARG1).length) { command[count++] = S8(BUSTER_HOST_C_COMPILER_ARG1); }
                command[count++] = source;
                command[count++] = S8("-L");
                command[count++] = root;
                command[count++] = S8("-lbuster1285_native");
                command[count++] = S8("-o");
                command[count++] = host_output;
                ProcessWaitResult controlled = compiler_driver_test_response_file_run(arena, (SliceString8){.pointer = command, .length = count});
                bool host_ok = !controlled.timed_out && controlled.result == PROCESS_RESULT_SUCCESS;
                BUSTER_TEST(arguments, host_ok);
                if (host_ok)
                {
                    host_ok = compiler_driver_test_process_success(arena, host_output);
                    BUSTER_TEST(arguments, host_ok);
                }
                String8 target = BUSTER_CPU_ARCH_AARCH64 ? S8("aarch64-linux") : S8("x86_64-linux");
                CompilerDriverResult direct = compiler_driver_archive_test_default_link(arena, target, sysroot, source, archive, (String8){0}, direct_output);
                UnitTestResult checked = compiler_driver_archive_test_default_result(arguments, direct, 91, (ByteSlice){0});
                result.test_count += checked.test_count;
                result.succeeded_test_count += checked.succeeded_test_count;
                CompilerDriverResult named = compiler_driver_archive_test_default_link(arena, target, sysroot, source,
                                                                                      S8("-lbuster1285_native"), (String8){0}, named_output);
                checked = compiler_driver_archive_test_default_result(arguments, named, 91, direct.native_link.executable);
                result.test_count += checked.test_count;
                result.succeeded_test_count += checked.succeeded_test_count;
                bool direct_ok = direct.error == COMPILER_DRIVER_ERROR_NONE && compiler_driver_test_process_success(arena, direct_output);
                bool named_ok = named.error == COMPILER_DRIVER_ERROR_NONE && compiler_driver_test_process_success(arena, named_output);
                BUSTER_TEST(arguments, direct_ok);
                BUSTER_TEST(arguments, named_ok);
                if (host_ok && direct_ok && named_ok)
                {
                    arguments->show(arguments, S8("DEFAULT_STATIC_LIBRARY_NATIVE_V1 target={S8} compiler={S8} archiver={S8} host_control=pass direct=pass sysroot_named=pass\n"),
                                    target, S8(BUSTER_HOST_C_COMPILER), archiver);
                }
            }
        }
    }
    scratch_end(temporary);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_lazy(UnitTestArguments* arguments)
{
    UnitTestResult result = compiler_driver_archive_test_default_roots(arguments);
    UnitTestResult native = compiler_driver_archive_test_default_native(arguments);
    result.test_count += native.test_count;
    result.succeeded_test_count += native.succeeded_test_count;
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_archive_test_aarch64_refusal_diagnostics);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_library_order_arguments);
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_library_order_execution);
    OperatingSystem systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_WINDOWS, OPERATING_SYSTEM_MACOS};
    for (u32 format = 0; format < BUSTER_ARRAY_LENGTH(systems); format += 1)
    {
        for (u32 kind = 0; kind < 5; kind += 1)
        {
            for (u32 indexed = 0; indexed < 2; indexed += 1)
            {
                TemporalArena temporary = arena_begin_temporal(arguments->arena);
                Arena* arena = arguments->arena;
                Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = systems[format]};
                Target foreign = {.cpu_arch = CPU_ARCH_AARCH64, .os = systems[format]};
                ObjectSymbol first[] = {
                    {.name = S8("foo"), .section = OBJECT_SECTION_DATA, .global = true},
                    {.name = S8("bar"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
                };
                ObjectSymbol unused[] = {{.name = S8("foreign"), .section = OBJECT_SECTION_DATA, .global = true}};
                ObjectSymbol next[] = {{.name = S8("bar"), .section = OBJECT_SECTION_DATA, .global = true}};
                ObjectSymbol duplicate[] = {{.name = S8("foo"), .section = OBJECT_SECTION_DATA, .global = true}};
                ObjectSymbol last[] = {{.name = S8("late"), .section = OBJECT_SECTION_DATA, .global = true}};
                ObjectFile members[] = {
                    compiler_driver_archive_test_object(arena, target, first, BUSTER_ARRAY_LENGTH(first), 1),
                    compiler_driver_archive_test_object(arena, foreign, unused, BUSTER_ARRAY_LENGTH(unused), 2),
                    compiler_driver_archive_test_object(arena, target, next, BUSTER_ARRAY_LENGTH(next), 3),
                    compiler_driver_archive_test_object(arena, target, duplicate, BUSTER_ARRAY_LENGTH(duplicate), 4),
                    compiler_driver_archive_test_object(arena, target, last, BUSTER_ARRAY_LENGTH(last), 5),
                };
                ByteSlice bytes = compiler_driver_archive_test_bytes(arena, members, BUSTER_ARRAY_LENGTH(members), kind);
                ObjectArchive archive = object_archive_read_link(arena, bytes, target);
                BUSTER_TEST(arguments, archive.error == OBJECT_ERROR_NONE && archive.object_count == BUSTER_ARRAY_LENGTH(members));
                BUSTER_TEST(arguments, object_archive_read(arena, bytes, target).error != OBJECT_ERROR_NONE);
                if (format == 1 && kind && archive.error == OBJECT_ERROR_NONE)
                {
                    // A data-only COFF member remains a valid i386 object
                    // after its machine field changes; no code or relocations
                    // in this member depend on the original architecture.
                    ByteSlice foreign_bytes = archive.member_bytes[1];
                    compiler_driver_archive_test_integer(foreign_bytes.pointer, 0x14c, 2, false);
                    ObjectArchive unrelated_i386 = object_archive_read_link(arena, bytes, target);
                    ObjectSymbol needed_foo = {.name = S8("foo"), .section = OBJECT_SECTION_UNDEFINED, .global = true};
                    ObjectFile i386_selected[6] = {compiler_driver_archive_test_object(arena, target, &needed_foo, 1, 0)};
                    u32 i386_count = 1;
                    CompilerDriverArchiveState i386_state = {0};
                    compiler_driver_archive_extract(arena, &i386_state, &unrelated_i386, i386_selected, &i386_count);
                    BUSTER_TEST(arguments, unrelated_i386.error == OBJECT_ERROR_NONE && i386_count == 3);
                    if (i386_state.arena) arena_destroy(i386_state.arena, 1);
                    compiler_driver_archive_test_integer(foreign_bytes.pointer, 0xaa64, 2, false);
                }
                if (BUSTER_REQUIRE(arguments, archive.error == OBJECT_ERROR_NONE))
                {
                    ObjectSymbol root_symbol = {.name = S8("foo"), .section = OBJECT_SECTION_UNDEFINED, .global = true};
                    ObjectFile root = compiler_driver_archive_test_object(arena, target, &root_symbol, 1, 0);
                    ObjectFile selected[8] = {root};
                    u32 count = 1;
                    CompilerDriverArchiveState state = {0};
                    if (indexed) state.arena = arena_create((ArenaCreation){.flags = {.no_pool = true}});
                    compiler_driver_archive_extract(arena, &state, &archive, selected, &count);
                    BUSTER_TEST(arguments, archive.error == OBJECT_ERROR_NONE && count == 3);
                    if (count == 3)
                    {
                        BUSTER_TEST(arguments, selected[1].sections[OBJECT_SECTION_DATA].data.pointer[0] == 1);
                        BUSTER_TEST(arguments, selected[2].sections[OBJECT_SECTION_DATA].data.pointer[0] == 3);
                        BUSTER_TEST(arguments, !archive.member_bytes[0].pointer && archive.member_bytes[1].pointer && !archive.member_bytes[2].pointer);
                    }
                    // A repeated archive sees references introduced by an
                    // ordinary object arriving after its first occurrence.
                    ObjectSymbol later = {.name = S8("late"), .section = OBJECT_SECTION_UNDEFINED, .global = true};
                    selected[count++] = compiler_driver_archive_test_object(arena, target, &later, 1, 0);
                    compiler_driver_archive_extract(arena, &state, &archive, selected, &count);
                    BUSTER_TEST(arguments, archive.error == OBJECT_ERROR_NONE && count == 5);
                    compiler_driver_archive_extract(arena, &state, &archive, selected, &count);
                    BUSTER_TEST(arguments, count == 5 && archive.member_bytes[1].pointer && archive.member_bytes[3].pointer);
                    if (state.arena) arena_destroy(state.arena, 1);
                    // No requested definitions means no payload is admitted.
                    ObjectArchive none = object_archive_read_link(arena, bytes, target);
                    root.symbol_count = 0;
                    selected[0] = root;
                    count = 1;
                    state = (CompilerDriverArchiveState){0};
                    compiler_driver_archive_extract(arena, &state, &none, selected, &count);
                    BUSTER_TEST(arguments, none.error == OBJECT_ERROR_NONE && count == 1 && none.member_bytes[0].pointer && none.member_bytes[1].pointer);
                    if (state.arena) arena_destroy(state.arena, 1);
                    // A genuine selected foreign member must still fail and
                    // name the member, actual architecture and requested one.
                    ObjectArchive needed = object_archive_read_link(arena, bytes, target);
                    root_symbol.name = S8("foreign");
                    root.symbol_count = 1;
                    selected[0] = root;
                    count = 1;
                    state = (CompilerDriverArchiveState){0};
                    compiler_driver_archive_extract(arena, &state, &needed, selected, &count);
                    BUSTER_TEST(arguments, needed.error != OBJECT_ERROR_NONE && needed.failed_member == 1 && count == 1);
                    BUSTER_TEST(arguments, string_first_sequence(needed.diagnostic, S8("member1.o")) != BUSTER_STRING_NO_MATCH);
                    BUSTER_TEST(arguments, string_first_sequence(needed.diagnostic, S8("aarch64")) != BUSTER_STRING_NO_MATCH);
                    BUSTER_TEST(arguments, string_first_sequence(needed.diagnostic, S8("x86_64")) != BUSTER_STRING_NO_MATCH);
                    if (state.arena) arena_destroy(state.arena, 1);
                    ObjectArchive weak = object_archive_read_link(arena, bytes, target);
                    root_symbol.weak = true;
                    selected[0] = root;
                    count = 1;
                    state = (CompilerDriverArchiveState){0};
                    compiler_driver_archive_extract(arena, &state, &weak, selected, &count);
                    BUSTER_TEST(arguments, format ? weak.error != OBJECT_ERROR_NONE : weak.error == OBJECT_ERROR_NONE && count == 1);
                    if (state.arena) arena_destroy(state.arena, 1);
                }
                scratch_end(temporary);
            }
        }
    }
    return result;
}

// R_X86_64_SIZE64 is an ordinary ELF size expression, unsupported by this
// reader. Its presence in an irrelevant member must not admit that payload.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_unused_size_relocation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
    for (u32 kind = 0; kind < 5; kind += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        Arena* arena = arguments->arena;
        ObjectSymbol needed_symbol = {.name = S8("needed"), .section = OBJECT_SECTION_DATA, .global = true};
        ObjectSymbol unused_symbol = {.name = S8("sized"), .section = OBJECT_SECTION_DATA, .global = true, .size = 8};
        ObjectFile members[] = {
            compiler_driver_archive_test_object(arena, target, &needed_symbol, 1, 1),
            compiler_driver_archive_test_object(arena, target, &unused_symbol, 1, 2),
        };
        u64 slot = 0;
        members[1].sections[OBJECT_SECTION_DATA].data = (ByteSlice){.pointer = (u8*)&slot, .length = sizeof(slot)};
        members[1].sections[OBJECT_SECTION_DATA].virtual_size = sizeof(slot);
        ObjectRelocation size = {.section = OBJECT_SECTION_DATA, .symbol = 0, .kind = OBJECT_RELOCATION_ABSOLUTE64};
        members[1].relocations = &size;
        members[1].relocation_count = 1;
        ByteSlice bytes = compiler_driver_archive_test_bytes(arena, members, BUSTER_ARRAY_LENGTH(members), kind);
        ObjectArchive archive = object_archive_read_link(arena, bytes, target);
        if (BUSTER_REQUIRE(arguments, archive.error == OBJECT_ERROR_NONE && archive.object_count == 2))
        {
            ByteSlice payload = archive.member_bytes[1];
            u64 table = 0;
            u16 count = 0;
            memcpy(&table, payload.pointer + 40, sizeof(table));
            memcpy(&count, payload.pointer + 60, sizeof(count));
            bool changed = false;
            for (u32 section = 0; section < count && !changed; section += 1)
            {
                u64 header = table + (u64)section * 64;
                u32 type = 0;
                memcpy(&type, payload.pointer + header + 4, sizeof(type));
                if (type == 4)
                {
                    u64 relocation_offset = 0;
                    memcpy(&relocation_offset, payload.pointer + header + 24, sizeof(relocation_offset));
                    compiler_driver_archive_test_integer(payload.pointer + relocation_offset + 8, 33, 4, false);
                    changed = true;
                }
            }
            BUSTER_TEST(arguments, changed);
            BUSTER_TEST(arguments, object_read(arena, payload, target).error == OBJECT_ERROR_UNSUPPORTED_TARGET);
            ObjectSymbol request = {.name = S8("needed"), .section = OBJECT_SECTION_UNDEFINED, .global = true};
            ObjectFile selected[3] = {compiler_driver_archive_test_object(arena, target, &request, 1, 0)};
            u32 selected_count = 1;
            CompilerDriverArchiveState state = {.arena = arena_create((ArenaCreation){.flags = {.no_pool = true}})};
            compiler_driver_archive_extract(arena, &state, &archive, selected, &selected_count);
            BUSTER_TEST(arguments, archive.error == OBJECT_ERROR_NONE && selected_count == 2 && archive.member_bytes[1].pointer);
            if (state.arena) arena_destroy(state.arena, 1);
            archive = object_archive_read_link(arena, bytes, target);
            request.name = S8("sized");
            selected_count = 1;
            state = (CompilerDriverArchiveState){0};
            compiler_driver_archive_extract(arena, &state, &archive, selected, &selected_count);
            BUSTER_TEST(arguments, archive.error == OBJECT_ERROR_UNSUPPORTED_TARGET && archive.failed_member == 1 && selected_count == 1);
            if (state.arena) arena_destroy(state.arena, 1);
        }
        scratch_end(temporary);
    }
    return result;
}
