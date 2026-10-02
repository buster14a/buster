// Valid mixed-target archive members, serialized with ordinary GNU/COFF and
// BSD ranlib indexes or no index. These controls exercise the real reader and
// both extraction paths without requiring a foreign-target executable.

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

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_lazy(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_archive_test_aarch64_refusal_diagnostics);
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

