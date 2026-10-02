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
    CompilerDriverResult compiled = compiler_driver_execute_invocation(arena, legacy);
    BUSTER_TEST_RAW(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE, compiled.diagnostic);
    if (compiled.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice image = file_read(arena, legacy_output, (FileReadOptions){0});
        BUSTER_TEST(arguments, image.length && image.length == forward.length && !memcmp(image.pointer, forward.pointer, image.length));
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
        }
        if (created.error.v == 0) BUSTER_TEST(arguments, os_directory_delete(root));
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_lazy(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
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

