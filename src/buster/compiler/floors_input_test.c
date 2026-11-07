// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included after floors_input.c by floors_benchmark.c. These discovery/validation tests run on every host ISA,
// without timing, pinning a CPU or allocating the benchmark's output buffer. All fixture bytes are original.

typedef struct FloorsInputTests FloorsInputTests;
struct FloorsInputTests
{
    unsigned checks;
    unsigned failures;
};

static void floors_input_expect(FloorsInputTests* tests, bool passed, const char* name)
{
    tests->checks += 1;
    tests->failures += passed ? 0 : 1;
    if (!passed)
    {
        fprintf(stderr, "floors input: %s\n", name);
    }
}

static void floors_input_test_close(FloorsInput* input)
{
    if (input->file >= 0)
    {
        close(input->file);
        input->file = -1;
    }
}

static bool floors_input_test_write(const char* path, const Elf64_Ehdr* header, size_t size)
{
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    bool result = file >= 0 && write(file, header, size) == (ssize_t)size;
    if (file >= 0)
    {
        result = close(file) == 0 && result;
    }
    return result;
}

static void floors_input_test_fixtures(FloorsInputTests* tests)
{
    char directory[BUSTER_FLOORS_INPUT_PATH_BYTES];
    const int length = snprintf(directory, sizeof(directory), "%s/buster-floors-input-XXXXXX", oracle_temporary_directory());
    const bool created = length > 0 && (size_t)length + sizeof("/fallback/libc.so.6") < sizeof(directory) && mkdtemp(directory) != NULL;
    floors_input_expect(tests, created, "create the isolated fixture directory");
    if (created)
    {
        char mapped_directory[BUSTER_FLOORS_INPUT_PATH_BYTES];
        char fallback_directory[BUSTER_FLOORS_INPUT_PATH_BYTES];
        char mapped_path[BUSTER_FLOORS_INPUT_PATH_BYTES];
        char fallback_path[BUSTER_FLOORS_INPUT_PATH_BYTES];
        char missing[BUSTER_FLOORS_INPUT_PATH_BYTES];
        snprintf(mapped_directory, sizeof(mapped_directory), "%s/mapped", directory);
        snprintf(fallback_directory, sizeof(fallback_directory), "%s/fallback", directory);
        snprintf(mapped_path, sizeof(mapped_path), "%s/mapped/libc.so.6", directory);
        snprintf(fallback_path, sizeof(fallback_path), "%s/fallback/libc.so.6", directory);
        snprintf(missing, sizeof(missing), "%s/missing", directory);
        const Elf64_Ehdr header = {
            .e_ident = {
                [EI_MAG0] = ELFMAG0,
                [EI_MAG1] = ELFMAG1,
                [EI_MAG2] = ELFMAG2,
                [EI_MAG3] = ELFMAG3,
                [EI_CLASS] = ELFCLASS64,
                [EI_DATA] = ELFDATA2LSB,
                [EI_VERSION] = EV_CURRENT,
            },
            .e_type = ET_DYN,
            .e_machine = EM_X86_64,
            .e_version = EV_CURRENT,
            .e_ehsize = sizeof(Elf64_Ehdr),
        };
        FILE* maps = tmpfile();
        bool ready = mkdir(mapped_directory, 0700) == 0 && mkdir(fallback_directory, 0700) == 0 && maps != NULL &&
                     floors_input_test_write(mapped_path, &header, sizeof(header)) && floors_input_test_write(fallback_path, &header, sizeof(header));
        floors_input_expect(tests, ready, "create the original ELF envelope fixtures and mapping stream");
        if (ready)
        {
            const char* const directories[] = { missing, fallback_directory };
            const char* const absent[] = { missing };
            FloorsInput input;
            int error = floors_input_select(&input, NULL, NULL, directories, 2, UINT64_MAX);
            floors_input_expect(tests, error == 0 && strcmp(input.path, fallback_path) == 0 &&
                                      strcmp(input.source, "runtime library directories") == 0 && input.status.st_size == (off_t)sizeof(header),
                                "a static host without mappings finds the first available runtime directory");
            floors_input_test_close(&input);

            fprintf(maps, "1000-2000 r--p 00000000 00:00 1 %s\n", mapped_path);
            rewind(maps);
            error = floors_input_select(&input, NULL, maps, directories, 2, UINT64_MAX);
            floors_input_expect(tests, error == 0 && strcmp(input.path, mapped_path) == 0 && strcmp(input.source, "/proc/self/maps") == 0,
                                "the dynamic host's mapped libc takes precedence over installed candidates");
            floors_input_test_close(&input);

            rewind(maps);
            error = floors_input_select(&input, fallback_path, maps, directories, 2, UINT64_MAX);
            floors_input_expect(tests, error == 0 && strcmp(input.path, fallback_path) == 0 && strcmp(input.source, "--libc") == 0,
                                "an explicit input takes precedence over both discovery routes");
            floors_input_test_close(&input);
            error = floors_input_select(&input, missing, maps, directories, 2, UINT64_MAX);
            floors_input_expect(tests, error == ENOENT && input.file == -1 && input.path[0] == 0,
                                "a missing explicit input reports ENOENT without silently falling back");
            error = floors_input_select(&input, NULL, NULL, absent, 1, UINT64_MAX);
            floors_input_expect(tests, error == ENOENT && input.file == -1, "a missing system runtime reports ENOENT");

            error = floors_input_select(&input, mapped_path, NULL, directories, 2, sizeof(header) - 1);
            floors_input_expect(tests, error == EFBIG && input.file == -1, "an input larger than the read buffer reports EFBIG");
            error = floors_input_select(&input, directory, NULL, directories, 2, UINT64_MAX);
            floors_input_expect(tests, error == EINVAL && input.file == -1, "a directory is not a regular shared-libc input");
            const bool fifo = mkfifo(missing, 0600) == 0;
            floors_input_expect(tests, fifo, "create a nonregular input");
            if (fifo)
            {
                error = floors_input_select(&input, missing, NULL, directories, 2, UINT64_MAX);
                floors_input_expect(tests, error == EINVAL && input.file == -1, "a FIFO is refused without waiting for a writer");
                unlink(missing);
            }

            char too_long[BUSTER_FLOORS_INPUT_PATH_BYTES + 1];
            memset(too_long, 'x', sizeof(too_long) - 1);
            too_long[sizeof(too_long) - 1] = 0;
            error = floors_input_select(&input, too_long, NULL, directories, 2, UINT64_MAX);
            floors_input_expect(tests, error == ENAMETOOLONG && input.file == -1, "an explicit path cannot truncate");

            for (unsigned invalid = 0; invalid < 7; invalid += 1)
            {
                Elf64_Ehdr changed = header;
                switch (invalid)
                {
                    case 0: changed.e_ident[EI_MAG0] = 0; break;
                    case 1: changed.e_ident[EI_CLASS] = ELFCLASS32; break;
                    case 2: changed.e_ident[EI_DATA] = ELFDATA2MSB; break;
                    case 3: changed.e_type = ET_EXEC; break;
                    case 4: changed.e_machine = EM_AARCH64; break;
                    case 5: changed.e_version = EV_NONE; break;
                    case 6: changed.e_ehsize = 0; break;
                }
                ready = floors_input_test_write(mapped_path, &changed, sizeof(changed));
                error = ready ? floors_input_select(&input, mapped_path, NULL, directories, 2, UINT64_MAX) : EIO;
                floors_input_expect(tests, error == ENOEXEC && input.file == -1, "an invalid explicit ELF envelope cannot fall back");
            }
            const char* const invalid_first[] = { mapped_directory, fallback_directory };
            error = floors_input_select(&input, NULL, NULL, invalid_first, 2, UINT64_MAX);
            floors_input_expect(tests, error == 0 && strcmp(input.path, fallback_path) == 0, "an unusable discovered candidate does not hide a valid later one");
            floors_input_test_close(&input);
            error = floors_input_select(&input, NULL, NULL, invalid_first, 1, UINT64_MAX);
            floors_input_expect(tests, error == ENOEXEC && input.file == -1, "an invalid installed runtime retains its diagnostic");

            for (size_t size = 0; size <= sizeof(header) - 1; size += sizeof(header) - 1)
            {
                ready = floors_input_test_write(mapped_path, &header, size);
                error = ready ? floors_input_select(&input, mapped_path, NULL, directories, 2, UINT64_MAX) : EIO;
                floors_input_expect(tests, error == ENOEXEC && input.file == -1, "empty and truncated ELF headers are refused");
            }
            ready = floors_input_test_write(mapped_path, &header, sizeof(header));
            floors_input_expect(tests, ready, "restore the mapped input");
            rewind(maps);
            for (unsigned i = 0; i < BUSTER_FLOORS_INPUT_PATH_BYTES + 512; i += 1)
            {
                fputc('x', maps);
            }
            fprintf(maps, " %s\n", mapped_path);
            rewind(maps);
            error = floors_input_select(&input, NULL, maps, directories, 2, UINT64_MAX);
            floors_input_expect(tests, error == 0 && strcmp(input.path, fallback_path) == 0, "an overlong mapping's tail cannot supply a different input");
            floors_input_test_close(&input);
        }
        if (maps != NULL)
        {
            fclose(maps);
        }
        unlink(mapped_path);
        unlink(fallback_path);
        rmdir(mapped_directory);
        rmdir(fallback_directory);
        rmdir(directory);
    }
}

int run_floors_input_tests(void);

int run_floors_input_tests(void)
{
    FloorsInputTests tests = { 0 };
    FloorsInput input;
    int error = floors_input_find(&input, NULL, UINT64_MAX);
    floors_input_expect(&tests, error == 0, "the real process finds a shared-libc input independent of its linkage");
    if (error == 0)
    {
        printf("floors input: process discovery used %s: %s\n", input.source, input.path);
    }
    floors_input_test_close(&input);
    error = floors_input_select(&input, NULL, NULL, runtime_library_directories,
                                sizeof(runtime_library_directories) / sizeof(runtime_library_directories[0]), UINT64_MAX);
    floors_input_expect(&tests, error == 0 && strcmp(input.source, "runtime library directories") == 0,
                        "the real installed runtime is discoverable without /proc/self/maps");
    floors_input_test_close(&input);
    floors_input_test_fixtures(&tests);
    printf("floors input: %u checks, %u failures\n", tests.checks, tests.failures);
    return tests.failures == 0 ? 0 : 1;
}
