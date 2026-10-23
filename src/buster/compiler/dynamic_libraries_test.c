// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Counted library metadata: an unversioned dependency, versioned dependencies at indices above 255, and foreign
// imports must survive the writer and loader with identical bytes at every lane count. Included by dynamic_test.c.

static bool dynamic_test_libraries(const ExecutableTestDirectory* directory, const Runtime* runtime)
{
    DynamicTestTally tally = { .check = "foreign library metadata" };
    DynamicTestImports program;
    dynamic_test_imports_program(&program, runtime);
    BindTestLink* test = &program.test;
    Scratch scratch = scratch_begin(0);
    ExecutableDynamic dynamic = { 0 };
    bool built = program.bound && dynamic_inputs(&dynamic, runtime, &test->bind, scratch.arena);
    ExecutableLibrary libraries[260] = { 0 };
    if (built)
    {
        libraries[1] = (ExecutableLibrary){ .name = BUSTER_S8("libm.so.6"), .required = true };
        libraries[258] = dynamic.libraries[0];
        libraries[259] = dynamic.libraries[1];
        libraries[259].required = true;
        ExecutableVersion* versions = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ExecutableVersion, dynamic.version_count);
        built = versions != 0;
        for (uint32_t v = 0; built && v < dynamic.version_count; v += 1)
        {
            versions[v] = dynamic.versions[v];
            versions[v].library += 258;
        }
        dynamic.versions = versions;
        dynamic.libraries = libraries;
        dynamic.library_count = BUSTER_ARRAY_LENGTH(libraries);
    }
    dynamic_test_expect(&tally, built, "the runtime and extra library prepared", 0);
    char path_bytes[BUSTER_EXECUTABLE_TEST_PATH_BYTES];
    const String8 path = executable_test_path(directory, "foreign-libraries", path_bytes);
    String8 reference = { 0 };
    for (uint32_t configuration = 0; built && configuration < 8; configuration += 1)
    {
        const uint32_t lanes = dynamic_test_lane_counts[configuration % 4];
        if (configuration == 1)
        {
            for (SymbolId symbol = 1; symbol < test->link.symbols.count; symbol += 1)
            {
                if (test->link.symbols.source[symbol] == BUSTER_LINK_SOURCE_RUNTIME_SHARED)
                {
                    test->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_FOREIGN_SHARED;
                }
            }
        }
        const bool reset = executable_test_reset(&test->link, path.pointer);
        const bool permuted = link_test_permute_names(&test->link, directory->id_order + configuration, scratch.arena);
        DynamicTestWrite write = {
            .link = &test->link,
            .options = {
                .path = path,
                .entry = dynamic_global(runtime, &test->bind, "_start"),
                .dynamic = &dynamic,
                .exact = configuration >= 4,
            },
            .poison = true,
        };
        const bool written = reset && permuted && lane_run(lanes, dynamic_test_lane_main, &write, 0) && write.succeeded;
        dynamic_test_expect(&tally, written, "library input writes", configuration);
        const String8 file = written ? executable_test_read(scratch.arena, path.pointer) : (String8){ 0 };
        const DynamicTestImage image = dynamic_test_image(file);
        const char* names[3] = { 0 };
        const uint32_t count = dynamic_test_needed(&image, names, BUSTER_ARRAY_LENGTH(names));
        dynamic_test_expect(&tally, image.valid && count == 3, "exactly three DT_NEEDED entries", configuration);
        if (count == 3)
        {
            dynamic_test_expect(&tally, strcmp(names[0], "libm.so.6") == 0 &&
                                strlen(names[1]) == libraries[258].name.length && memcmp(names[1], libraries[258].name.pointer, libraries[258].name.length) == 0 &&
                                strlen(names[2]) == libraries[259].name.length && memcmp(names[2], libraries[259].name.pointer, libraries[259].name.length) == 0,
                                "dependency order, including unversioned and high-index libraries", configuration);
        }
        if (configuration == 0)
        {
            reference = file;
        }
        dynamic_test_expect(&tally, file.length != 0 && file.length == reference.length && memcmp(file.pointer, reference.pointer, file.length) == 0,
                            "runtime/foreign sources, lane counts and exact path agree", configuration);
        dynamic_test_expect(&tally, written && executable_test_runs(path.pointer, (const uint8_t*)"", 0), "loader resolves every dependency and version", configuration);
    }
    // Invalid metadata must fail on every lane before opening an output, rather than indexing a missing library or
    // silently truncating a library/version index. Use the real writer so the broadcast and cleanup are tested too.
    SymbolId imported = 0;
    for (SymbolId symbol = 1; symbol < test->link.symbols.count && imported == 0; symbol += 1)
    {
        imported = test->link.symbols.source[symbol] == BUSTER_LINK_SOURCE_FOREIGN_SHARED ? symbol : 0;
    }
    const uint16_t saved_version = test->link.symbols.version[imported];
    for (uint32_t bad = 0; built && bad < 9; bad += 1)
    {
        ExecutableDynamic invalid = dynamic;
        ExecutableVersion version = dynamic.versions[0];
        ExecutableVersion pair[2] = { version, version };
        if (bad == 0)
        {
            invalid.libraries = 0;
        }
        else if (bad == 1)
        {
            version.library = dynamic.library_count;
            invalid.versions = &version;
            invalid.version_count = 1;
        }
        else if (bad == 2)
        {
            version.index = 0x8000;
            invalid.versions = &version;
            invalid.version_count = 1;
        }
        else if (bad == 3)
        {
            invalid.versions = 0;
        }
        else if (bad == 4)
        {
            invalid.library_count = UINT32_MAX;
        }
        else if (bad == 5 || bad == 6)
        {
            // Duplicate indices, then distinct indices with libraries in descending order.
            pair[0].index = 2;
            pair[1].index = bad == 5 ? 2 : 3;
            pair[0].library = 259;
            pair[1].library = bad == 5 ? 259 : 258;
            invalid.versions = pair;
            invalid.version_count = 2;
        }
        else
        {
            // The symbol pass must reject both a gap inside the metadata's index range and an index above it.
            pair[0].index = 2;
            pair[1].index = 4;
            invalid.versions = pair;
            invalid.version_count = 2;
            test->link.symbols.version[imported] = bad == 7 ? 3 : UINT16_MAX;
        }
        unlink(path.pointer);
        DynamicTestWrite write = {
            .link = &test->link,
            .options = { .path = path, .entry = dynamic_global(runtime, &test->bind, "_start"), .dynamic = &invalid },
        };
        const bool ran = lane_run(8, dynamic_test_lane_main, &write, 0);
        dynamic_test_expect(&tally, ran && !write.succeeded && write.result.error == EXECUTABLE_ERROR_UNSUPPORTED && access(path.pointer, F_OK) != 0,
                            "malformed metadata refused without publication", bad);
        test->link.symbols.version[imported] = saved_version;
    }
    scratch_end(scratch);
    if (!directory->keep)
    {
        unlink(path.pointer);
    }
    bind_test_link_end(test);
    return dynamic_test_report(&tally);
}
