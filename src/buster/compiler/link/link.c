#include <buster/compiler/link/link.h>

#include <buster/file.h>
#include <buster/integer.h>
#include <buster/os.h>
#include <buster/string.h>

BUSTER_GLOBAL_LOCAL String8 link_string_copy(
    Arena* arena,
    String8 source)
{
    String8 result = {0};
    if (source.length)
    {
        result.pointer = arena_allocate(
            arena,
            char8,
            source.length);
        result.length = source.length;
        memcpy(
            result.pointer,
            source.pointer,
            source.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool link_target_matches(
    Target left,
    Target right)
{
    return
        left.cpu_arch == right.cpu_arch &&
        left.os == right.os;
}

BUSTER_GLOBAL_LOCAL u32 link_global_symbol_find(
    ObjectSymbol* symbols,
    u32 symbol_count,
    String8 name)
{
    for (u32 index = 0;
        index < symbol_count;
        index += 1)
    {
        if (symbols[index].global &&
            string_equal(symbols[index].name, name))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL bool link_symbol_definition_set(
    ObjectSymbol* destination,
    ObjectSymbol* source,
    ObjectFile* object,
    u64* section_offsets,
    Arena* arena)
{
    *destination = *source;
    destination->name =
        link_string_copy(arena, source->name);
    if (source->section == OBJECT_SECTION_UNDEFINED)
    {
        return true;
    }
    if (source->section >= object->section_count)
    {
        return false;
    }
    ObjectSectionKind kind =
        object->sections[source->section].kind;
    if (kind >= OBJECT_SECTION_COUNT)
    {
        return false;
    }
    destination->section = (u32)kind;
    destination->value +=
        section_offsets[source->section];
    return true;
}

LinkObjectResult link_objects(
    Arena* arena,
    ObjectFile* objects,
    u32 object_count,
    LinkOptions options)
{
    LinkObjectResult result = {0};
    if (!arena || !objects || !object_count)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    Target target = objects[0].target;
    u64 section_sizes[OBJECT_SECTION_COUNT] = {0};
    u32 section_alignments[OBJECT_SECTION_COUNT] = {
        [OBJECT_SECTION_TEXT] = 1,
        [OBJECT_SECTION_READ_ONLY_DATA] = 1,
        [OBJECT_SECTION_DATA] = 1,
    };
    u64 total_symbols = 0;
    u64 total_relocations = 0;
    u64 offset_count =
        (u64)object_count * OBJECT_SECTION_COUNT;
    u64* section_offsets = arena_allocate(
        arena,
        u64,
        offset_count);
    for (u32 object_index = 0;
        object_index < object_count;
        object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        if (object->error != OBJECT_ERROR_NONE ||
            !object->sections ||
            object->section_count >
                OBJECT_SECTION_COUNT ||
            (object->symbol_count && !object->symbols) ||
            (object->relocation_count &&
                !object->relocations))
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        if (!link_target_matches(target, object->target))
        {
            result.error = LINK_ERROR_TARGET_MISMATCH;
            return result;
        }
        total_symbols += object->symbol_count;
        total_relocations += object->relocation_count;
        if (total_symbols > UINT32_MAX ||
            total_relocations > UINT32_MAX)
        {
            result.error = LINK_ERROR_INVALID_INPUT;
            return result;
        }
        for (u32 section_index = 0;
            section_index < object->section_count;
            section_index += 1)
        {
            ObjectSection* section =
                &object->sections[section_index];
            if (section->kind >=
                    OBJECT_SECTION_COUNT ||
                !section->alignment ||
                !BUSTER_IS_POWER_OF_TWO(
                    section->alignment) ||
                (section->data.length &&
                    !section->data.pointer))
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            u32 alignment = section->alignment;
            u64 aligned = align_forward(
                section_sizes[section->kind],
                alignment);
            if (aligned <
                    section_sizes[section->kind] ||
                section->data.length >
                    UINT64_MAX - aligned)
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            section_offsets[
                (u64)object_index *
                    OBJECT_SECTION_COUNT +
                section_index] = aligned;
            section_sizes[section->kind] =
                aligned + section->data.length;
            section_alignments[section->kind] =
                BUSTER_MAX(
                    section_alignments[section->kind],
                    alignment);
        }
    }
    result.object = (ObjectFile){
        .sections = arena_allocate(
            arena,
            ObjectSection,
            OBJECT_SECTION_COUNT),
        .symbols = arena_allocate(
            arena,
            ObjectSymbol,
            total_symbols),
        .relocations = arena_allocate(
            arena,
            ObjectRelocation,
            total_relocations),
        .target = target,
        .section_count = OBJECT_SECTION_COUNT,
    };
    String8 section_names[OBJECT_SECTION_COUNT] = {
        [OBJECT_SECTION_TEXT] = S8(".text"),
        [OBJECT_SECTION_READ_ONLY_DATA] =
            S8(".rodata"),
        [OBJECT_SECTION_DATA] = S8(".data"),
    };
    for (u32 kind = 0;
        kind < OBJECT_SECTION_COUNT;
        kind += 1)
    {
        u8* data = arena_allocate(
            arena,
            u8,
            section_sizes[kind]);
        result.object.sections[kind] =
            (ObjectSection){
                .name = section_names[kind],
                .data = {
                    .pointer = data,
                    .length = section_sizes[kind],
                },
                .kind = (ObjectSectionKind)kind,
                .alignment =
                    section_alignments[kind],
            };
    }
    for (u32 object_index = 0;
        object_index < object_count;
        object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u64* offsets =
            section_offsets +
            (u64)object_index *
                OBJECT_SECTION_COUNT;
        for (u32 section_index = 0;
            section_index < object->section_count;
            section_index += 1)
        {
            ObjectSection* source =
                &object->sections[section_index];
            if (source->data.length)
            {
                memcpy(
                    result.object.sections[
                        source->kind].data.pointer +
                        offsets[section_index],
                    source->data.pointer,
                    source->data.length);
            }
        }
    }
    u32** symbol_maps = arena_allocate(
        arena,
        u32*,
        object_count);
    for (u32 object_index = 0;
        object_index < object_count;
        object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u32* symbol_map = arena_allocate(
            arena,
            u32,
            object->symbol_count);
        symbol_maps[object_index] = symbol_map;
        for (u32 source_index = 0;
            source_index < object->symbol_count;
            source_index += 1)
        {
            symbol_map[source_index] = UINT32_MAX;
            ObjectSymbol* source =
                &object->symbols[source_index];
            if (!source->name.length)
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            if (source->section !=
                    OBJECT_SECTION_UNDEFINED &&
                (source->section >=
                        object->section_count ||
                    source->value >
                        object->sections[
                            source->section]
                            .data.length ||
                    source->size >
                        object->sections[
                            source->section]
                            .data.length -
                            source->value))
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            u32 destination_index = UINT32_MAX;
            if (source->global)
            {
                destination_index =
                    link_global_symbol_find(
                        result.object.symbols,
                        result.object.symbol_count,
                        source->name);
            }
            if (destination_index == UINT32_MAX)
            {
                destination_index =
                    result.object.symbol_count++;
                if (!link_symbol_definition_set(
                        &result.object.symbols[
                            destination_index],
                        source,
                        object,
                        section_offsets +
                            (u64)object_index *
                                OBJECT_SECTION_COUNT,
                        arena))
                {
                    result.error =
                        LINK_ERROR_INVALID_INPUT;
                    return result;
                }
            }
            else
            {
                ObjectSymbol* destination =
                    &result.object.symbols[
                        destination_index];
                bool destination_defined =
                    destination->section !=
                        OBJECT_SECTION_UNDEFINED;
                bool source_defined =
                    source->section !=
                        OBJECT_SECTION_UNDEFINED;
                if (destination_defined &&
                    source_defined)
                {
                    result.error =
                        LINK_ERROR_DUPLICATE_SYMBOL;
                    result.symbol =
                        link_string_copy(
                            arena,
                            source->name);
                    return result;
                }
                if (!destination_defined &&
                    source_defined)
                {
                    if (!link_symbol_definition_set(
                            destination,
                            source,
                            object,
                            section_offsets +
                                (u64)object_index *
                                    OBJECT_SECTION_COUNT,
                            arena))
                    {
                        result.error =
                            LINK_ERROR_INVALID_INPUT;
                        return result;
                    }
                }
            }
            symbol_map[source_index] =
                destination_index;
        }
    }
    for (u32 object_index = 0;
        object_index < object_count;
        object_index += 1)
    {
        ObjectFile* object = &objects[object_index];
        u64* offsets =
            section_offsets +
            (u64)object_index *
                OBJECT_SECTION_COUNT;
        for (u32 relocation_index = 0;
            relocation_index <
                object->relocation_count;
            relocation_index += 1)
        {
            ObjectRelocation source =
                object->relocations[relocation_index];
            if (source.section >=
                    object->section_count ||
                source.symbol >=
                    object->symbol_count ||
                symbol_maps[object_index][
                    source.symbol] == UINT32_MAX)
            {
                result.error =
                    LINK_ERROR_INVALID_INPUT;
                return result;
            }
            ObjectSectionKind kind =
                object->sections[
                    source.section].kind;
            source.section = (u32)kind;
            source.offset +=
                offsets[
                    object->relocations[
                        relocation_index].section];
            source.symbol =
                symbol_maps[object_index][
                    source.symbol];
            result.object.relocations[
                result.object.relocation_count++] =
                    source;
        }
    }
    if (!options.allow_undefined_symbols)
    {
        for (u32 symbol_index = 0;
            symbol_index <
                result.object.symbol_count;
            symbol_index += 1)
        {
            ObjectSymbol* symbol =
                &result.object.symbols[symbol_index];
            if (symbol->section ==
                OBJECT_SECTION_UNDEFINED)
            {
                result.error =
                    LINK_ERROR_UNRESOLVED_SYMBOL;
                result.symbol =
                    link_string_copy(
                        arena,
                        symbol->name);
                return result;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool link_host_supported(
    Target target)
{
#if BUSTER_ANDROID || BUSTER_IOS
    BUSTER_UNUSED(target);
    return false;
#else
    return link_target_matches(target, target_native) &&
        (target.os == OPERATING_SYSTEM_LINUX ||
            target.os == OPERATING_SYSTEM_MACOS ||
            target.os == OPERATING_SYSTEM_WINDOWS);
#endif
}

LibcLinkResult link_object_with_libc(
    Arena* arena,
    ObjectFile* object,
    LibcLinkOptions options)
{
    LibcLinkResult result = {
        .process_result = PROCESS_RESULT_UNKNOWN,
    };
    if (!arena || !object ||
        object->error != OBJECT_ERROR_NONE ||
        !options.output_path.length ||
        !options.object_path.length)
    {
        result.error = LINK_ERROR_INVALID_INPUT;
        return result;
    }
    if (!link_host_supported(object->target))
    {
        result.error = LINK_ERROR_UNSUPPORTED_HOST;
        return result;
    }
    ObjectArtifact artifact = object_write(
        arena,
        object,
        object_format_for_target(object->target));
    if (artifact.error != OBJECT_ERROR_NONE)
    {
        result.error = LINK_ERROR_OBJECT_WRITE;
        return result;
    }
    if (!file_write(options.object_path, artifact.bytes))
    {
        result.error = LINK_ERROR_FILE_WRITE;
        return result;
    }
    String8 linker = options.linker_executable;
    if (!linker.length)
    {
#if BUSTER_WINDOWS
        linker = S8("clang");
#else
        linker = S8("cc");
#endif
    }
    String8 spawn_arguments[] = {
        linker,
        options.object_path,
        S8("-o"),
        options.output_path,
    };
    ProcessSpawnOptions spawn_options = {
        .capture =
            ((u64)1 << STANDARD_STREAM_OUTPUT) |
            ((u64)1 << STANDARD_STREAM_ERROR),
        .use_process_environment = true,
    };
    ProcessSpawnResult spawn = os_process_spawn(
        (SliceString8)
            BUSTER_ARRAY_TO_SLICE(spawn_arguments),
        (SliceString8){0},
        (SliceString8){0},
        spawn_options);
    if (!spawn.handle)
    {
        result.error = LINK_ERROR_PROCESS_SPAWN;
        return result;
    }
    ProcessWaitResult wait =
        os_process_wait_sync(arena, spawn);
    result.standard_output =
        wait.streams[STANDARD_STREAM_OUTPUT];
    result.standard_error =
        wait.streams[STANDARD_STREAM_ERROR];
    result.process_result = wait.result;
    if (wait.result != PROCESS_RESULT_SUCCESS)
    {
        result.error = LINK_ERROR_PROCESS_FAILED;
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL ObjectFile link_test_object_make(
    Arena* arena,
    Target target,
    ByteSlice text,
    ObjectSymbol* symbols,
    u32 symbol_count,
    ObjectRelocation* relocations,
    u32 relocation_count)
{
    ObjectSection* sections = arena_allocate(
        arena,
        ObjectSection,
        OBJECT_SECTION_COUNT);
    sections[OBJECT_SECTION_TEXT] =
        (ObjectSection){
            .name = S8(".text"),
            .data = text,
            .kind = OBJECT_SECTION_TEXT,
            .alignment = 16,
        };
    sections[OBJECT_SECTION_READ_ONLY_DATA] =
        (ObjectSection){
            .name = S8(".rodata"),
            .kind =
                OBJECT_SECTION_READ_ONLY_DATA,
            .alignment = 8,
        };
    sections[OBJECT_SECTION_DATA] =
        (ObjectSection){
            .name = S8(".data"),
            .kind = OBJECT_SECTION_DATA,
            .alignment = 8,
        };
    return (ObjectFile){
        .sections = sections,
        .symbols = symbols,
        .relocations = relocations,
        .target = target,
        .section_count = OBJECT_SECTION_COUNT,
        .symbol_count = symbol_count,
        .relocation_count = relocation_count,
    };
}

UnitTestResult link_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target target = target_native;
#if BUSTER_CPU_ARCH_X86_64
    u8 answer_text[] = {
        0xb8, 42, 0, 0, 0,
        0xc3,
    };
    u8 main_text[] = {
        0xe8, 0, 0, 0, 0,
        0x83, 0xe8, 42,
        0xc3,
    };
    ByteSlice answer_bytes =
        (ByteSlice)
            BUSTER_ARRAY_TO_SLICE(answer_text);
    ByteSlice main_bytes =
        (ByteSlice)
            BUSTER_ARRAY_TO_SLICE(main_text);
    ObjectRelocation main_relocation = {
        .addend = -4,
        .offset = 1,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind =
            OBJECT_RELOCATION_X86_64_PC32,
    };
#elif BUSTER_CPU_ARCH_AARCH64
    u32 answer_instructions[] = {
        0x52800540,
        0xd65f03c0,
    };
    u32 main_instructions[] = {
        0xa9bf7bfd,
        0x910003fd,
        0x94000000,
        0x5100a800,
        0xa8c17bfd,
        0xd65f03c0,
    };
    ByteSlice answer_bytes = {
        .pointer = (u8*)answer_instructions,
        .length = sizeof(answer_instructions),
    };
    ByteSlice main_bytes = {
        .pointer = (u8*)main_instructions,
        .length = sizeof(main_instructions),
    };
    ObjectRelocation main_relocation = {
        .offset = 8,
        .section = OBJECT_SECTION_TEXT,
        .symbol = 1,
        .kind =
            OBJECT_RELOCATION_AARCH64_CALL26,
    };
#endif
#if BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64
    ObjectSymbol answer_symbols[] = {
        {
            .name = S8("answer"),
            .size = answer_bytes.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectSymbol main_symbols[] = {
        {
            .name = S8("main"),
            .size = main_bytes.length,
            .section = OBJECT_SECTION_TEXT,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
        {
            .name = S8("answer"),
            .section = OBJECT_SECTION_UNDEFINED,
            .kind = OBJECT_SYMBOL_FUNCTION,
            .global = true,
        },
    };
    ObjectFile objects[] = {
        link_test_object_make(
            arguments->arena,
            target,
            answer_bytes,
            answer_symbols,
            BUSTER_ARRAY_LENGTH(answer_symbols),
            0,
            0),
        link_test_object_make(
            arguments->arena,
            target,
            main_bytes,
            main_symbols,
            BUSTER_ARRAY_LENGTH(main_symbols),
            &main_relocation,
            1),
    };
    LinkObjectResult linked = link_objects(
        arguments->arena,
        objects,
        BUSTER_ARRAY_LENGTH(objects),
        (LinkOptions){0});
    BUSTER_TEST(arguments,
        linked.error == LINK_ERROR_NONE);
    BUSTER_TEST(arguments,
        linked.object.symbol_count == 2);
    BUSTER_TEST(arguments,
        linked.object.relocation_count == 1);
    bool linked_text_size_matches = false;
    if (linked.object.sections &&
        linked.object.section_count >
            OBJECT_SECTION_TEXT)
    {
        linked_text_size_matches =
            linked.object.sections[
                OBJECT_SECTION_TEXT].data.length ==
                    align_forward(
                        answer_bytes.length,
                        16) +
                    main_bytes.length;
    }
    BUSTER_TEST(arguments,
        linked_text_size_matches);
    ObjectFile duplicate_objects[] = {
        objects[0],
        objects[0],
    };
    LinkObjectResult duplicate = link_objects(
        arguments->arena,
        duplicate_objects,
        BUSTER_ARRAY_LENGTH(duplicate_objects),
        (LinkOptions){0});
    BUSTER_TEST(arguments,
        duplicate.error ==
            LINK_ERROR_DUPLICATE_SYMBOL);
    BUSTER_STRING_TEST(
        arguments,
        duplicate.symbol,
        S8("answer"));
    ObjectFile unresolved_object = objects[1];
    LinkObjectResult unresolved = link_objects(
        arguments->arena,
        &unresolved_object,
        1,
        (LinkOptions){0});
    BUSTER_TEST(arguments,
        unresolved.error ==
            LINK_ERROR_UNRESOLVED_SYMBOL);
    BUSTER_STRING_TEST(
        arguments,
        unresolved.symbol,
        S8("answer"));
    LinkObjectResult permitted = link_objects(
        arguments->arena,
        &unresolved_object,
        1,
        (LinkOptions){
            .allow_undefined_symbols = true,
        });
    BUSTER_TEST(arguments,
        permitted.error == LINK_ERROR_NONE);
#if BUSTER_LINK_LIBC && !BUSTER_ANDROID && !BUSTER_IOS && \
    !BUSTER_SANITIZE
#if BUSTER_WINDOWS
    String8 object_path =
        S8("build/buster-link-test.obj");
    String8 output_path =
        S8("build/buster-link-test.exe");
#else
    String8 object_path =
        S8("/tmp/buster-link-test.o");
    String8 output_path =
        S8("/tmp/buster-link-test");
#endif
    LibcLinkResult executable =
        link_object_with_libc(
            arguments->arena,
            &linked.object,
            (LibcLinkOptions){
                .output_path = output_path,
                .object_path = object_path,
            });
    BUSTER_TEST(arguments,
        executable.error == LINK_ERROR_NONE);
    if (executable.error == LINK_ERROR_NONE)
    {
        String8 run_arguments[] = {
            output_path,
        };
        ProcessSpawnResult spawn = os_process_spawn(
            (SliceString8)
                BUSTER_ARRAY_TO_SLICE(
                    run_arguments),
            (SliceString8){0},
            (SliceString8){0},
            (ProcessSpawnOptions){
                .use_process_environment = true,
            });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait =
                os_process_wait_sync(
                    arguments->arena,
                    spawn);
            BUSTER_TEST(arguments,
                wait.result ==
                    PROCESS_RESULT_SUCCESS);
        }
    }
#endif
#else
    BUSTER_UNUSED(target);
#endif
    return result;
}
#endif
