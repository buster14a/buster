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

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_stack_note(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CpuArch architectures[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    for (u32 architecture = 0; architecture < BUSTER_ARRAY_LENGTH(architectures); architecture += 1)
    {
        Target target = {.cpu_arch = architectures[architecture], .os = OPERATING_SYSTEM_LINUX};
        ObjectSymbol definitions[] = {
            {.name = S8("safe"), .section = OBJECT_SECTION_DATA, .global = true},
            {.name = S8("exec"), .section = OBJECT_SECTION_DATA, .global = true},
            {.name = S8("unused_exec"), .section = OBJECT_SECTION_DATA, .global = true},
        };
        ObjectFile members[3];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(members); index += 1)
        {
            members[index] = compiler_driver_archive_test_object(arguments->arena, target, definitions + index, 1, (u8)index);
            members[index].requires_executable_stack = index != 0;
        }
        ByteSlice bytes = compiler_driver_archive_test_bytes(arguments->arena, members, BUSTER_ARRAY_LENGTH(members), 1);
        ObjectArchive eager = object_archive_read(arguments->arena, bytes, target);
        BUSTER_TEST(arguments, eager.error == OBJECT_ERROR_NONE && eager.object_count == BUSTER_ARRAY_LENGTH(members));
        if (eager.error == OBJECT_ERROR_NONE && eager.object_count == BUSTER_ARRAY_LENGTH(members))
        {
            BUSTER_TEST(arguments, eager.objects[1].requires_executable_stack && eager.objects[2].requires_executable_stack && !eager.objects[0].requires_executable_stack);
            BUSTER_STRING_TEST(arguments, eager.objects[1].executable_stack_source, S8("member1.o"));
        }
        for (u32 indexed = 0; indexed < 2; indexed += 1)
        {
            for (u32 requested = 0; requested < 2; requested += 1)
            {
                ObjectArchive archive = object_archive_read_link(arguments->arena, bytes, target);
                ObjectSymbol needed = {.name = requested ? S8("exec") : S8("safe"), .section = OBJECT_SECTION_UNDEFINED, .global = true};
                ObjectFile selected[4] = {compiler_driver_archive_test_object(arguments->arena, target, &needed, 1, 0)};
                u32 count = 1;
                CompilerDriverArchiveState state = {0};
                if (indexed) state.arena = arena_create((ArenaCreation){.flags = {.no_pool = true}});
                compiler_driver_archive_extract(arguments->arena, &state, &archive, selected, &count);
                BUSTER_TEST(arguments, archive.error == OBJECT_ERROR_NONE && count == 2);
                LinkObjectResult merged = link_objects(arguments->arena, selected, count, (LinkOptions){0});
                BUSTER_TEST(arguments, merged.error == LINK_ERROR_NONE && merged.object.requires_executable_stack == (requested != 0));
                if (requested && merged.error == LINK_ERROR_NONE)
                {
                    NativeExecutableLinkResult linked = link_native_executable(arguments->arena, &merged.object, (NativeExecutableLinkOptions){0});
                    BUSTER_TEST(arguments, linked.error == LINK_ERROR_UNSUPPORTED_FEATURE && !linked.executable.length);
                    BUSTER_TEST(arguments, string_starts_with_sequence(linked.symbol, S8("member1.o: executable-stack request")));
                }
                if (state.arena) arena_destroy(state.arena, 1);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_lazy(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, compiler_driver_archive_test_stack_note);
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

