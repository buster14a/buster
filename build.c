#if 0
#!/usr/bin/env bash
set -eu

if [[ -z "${BUSTER_CI:-}" ]]; then
    BUSTER_CI=0
fi

if [[ "$#" != "0" ]]; then
    set -x
fi

if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
    export CLANG=$(which clang)
else
    export CLANG=$CMAKE_PREFIX_PATH/bin/clang
fi

#if 0BUSTER_REGENERATE=0 build/builder $@ 2>/dev/null
#endif
#if [[ "$?" != "0" && "$?" != "333" ]]; then
    mkdir -p build
    $CLANG build.c -o build/builder -Isrc -std=gnu2x -march=native -DBUSTER_UNITY_BUILD=1 -DBUSTER_USE_IO_RING=1 -DBUSTER_USE_PTHREAD=1 -g -Werror -Wall -Wextra -Wpedantic -pedantic -Wno-gnu-auto-type -Wno-gnu-empty-struct -Wno-bitwise-instead-of-logical -Wno-unused-function -luring -pthread #-ferror-limit=1 -ftime-trace -ftime-trace-verbose
    if [[ "$?" == "0" ]]; then
        BUSTER_REGENERATE=1 build/builder $@
    fi
#endif fi
exit $?
#endif

#include <lib.h>

#if BUSTER_UNITY_BUILD
#include <lib.c>
#endif
#include <md5.h>
#include <stdio.h>

#define BUSTER_TODO() BUSTER_TRAP()

STRUCT(Files)
{
    u16* pointer;
    u64 count;
};

typedef enum WorkFileStatus : u8
{
    WORK_FILE_OPENING,
    WORK_FILE_OPEN_ERROR,
    WORK_FILE_STATING,
    WORK_FILE_STATED,
    WORK_FILE_READING,
    WORK_FILE_READ,
} WorkFileStatus;

typedef enum CompilationModel
{
    COMPILATION_MODEL_INCREMENTAL,
    COMPILATION_MODEL_SINGLE_UNIT,
} CompilationModel;

STRUCT(WorkFile)
{
    u128 hash;
    struct statx statx;
    u64 time_s;
    u64 time_ns;
    u64 size;
    u64 arena_offset;
    int fd;
    WorkFileStatus status;
    u8 cache_padding[BUSTER_MIN(BUSTER_CACHE_LINE_GUESS - ((0 * sizeof(u64))), BUSTER_CACHE_LINE_GUESS)];
};
static_assert(sizeof(WorkFile) % BUSTER_CACHE_LINE_GUESS == 0);

STRUCT(OldFile)
{
    u8 cache_padding[BUSTER_CACHE_LINE_GUESS];
};

static_assert(sizeof(OldFile) % BUSTER_CACHE_LINE_GUESS == 0);

STRUCT(Artifact)
{
    String path;
    Files file;
};

BUSTER_LOCAL String file_relative_paths[] = {
    S("build/cache_manifest"),
    S("build.c"),
    S("src/lib.h"),
    S("src/lib.c"),
    S("src/meow_hash_x64_aesni.h"),
    S("src/disk_builder.c"),
};

typedef enum BuildCommand
{
    BUILD_COMMAND_BUILD,
    BUILD_COMMAND_TEST,
    BUILD_COMMAND_DEBUG,
} BuildCommand;

STRUCT(ProgramInput)
{
    BuildCommand command;
};

BUSTER_LOCAL WorkFile work_files[BUSTER_ARRAY_LENGTH(file_relative_paths)] = {};

BUSTER_LOCAL void work_file_open(WorkFile* file, String path, u64 user_data)
{
    io_ring_prepare_open((char*)path.pointer, user_data);
    file->status = WORK_FILE_OPENING;
    file->fd = -1;
}

BUSTER_LOCAL u128 hash_file(const char* pointer, u64 length)
{
    BUSTER_CHECK(((u64)pointer & (64 - 1)) == 0);

    md5_ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, pointer, length);
    u128 digest;
    static_assert(sizeof(digest) == MD5_DIGEST_SIZE);
    md5_finish(&ctx, (u8*)&digest);
    return digest;
}

STRUCT(Process)
{
    siginfo_t siginfo;
    pid_t pid;
    char** argv;
    char** envp;
    bool waited;
};

static_assert(alignof(Process) == 8);

ProcessResult process_query_wait(Process* p)
{
    ProcessResult result;
    if (p->pid != -1)
    {
        if (p->waited)
        {
            // Then we are allowed to query the siginfo struct
            if (BUSTER_LIKELY(p->siginfo.si_code == CLD_EXITED))
            {
                result = (ProcessResult)p->siginfo.si_status;
            }
            else
            {
                BUSTER_TODO();
            }
        }
        else
        {
            result = PROCESS_RESULT_RUNNING;
        }
    }
    else
    {
        result = PROCESS_RESULT_NOT_EXISTENT;
    }
    
    return result;
}

BUSTER_LOCAL void spawn_process(Process* process, char* argv[], char* envp[])
{
    let pid = fork();

    if (pid == -1)
    {
        printf("Failed to fork\n");
    }
    else if (pid == 0)
    {
        execve(argv[0], argv, envp);
        BUSTER_TRAP();
    }

    if (global_program.verbose)
    {
        printf("Launched: ");

        for (let a = argv; *a; a += 1)
        {
            printf("%s ", *a);
        }

        printf("\n");
    }

    *process = (Process) {
        .argv = argv,
        .envp = envp,
        .pid = pid,
    };
}

typedef enum TaskId
{
    TASK_ID_COMPILATION,
    TASK_ID_LINKING,
} TaskId;

typedef enum ProjectId
{
    PROJECT_OPERATING_SYSTEM_BUILDER,
    PROJECT_OPERATING_SYSTEM_BOOTLOADER,
    PROJECT_OPERATING_SYSTEM_KERNEL,
    PROJECT_COUNT,
} ProjectId;

typedef enum CpuArch
{
    CPU_ARCH_X86_64,
} CpuArch;

typedef enum CpuModel
{
    CPU_MODEL_GENERIC,
    CPU_MODEL_NATIVE,
} CpuModel;

typedef enum OperatingSystem
{
    OPERATING_SYSTEM_LINUX,
    OPERATING_SYSTEM_MACOS,
    OPERATING_SYSTEM_WINDOWS,
    OPERATING_SYSTEM_UEFI,
    OPERATING_SYSTEM_FREESTANDING,
} OperatingSystem;

STRUCT(Target)
{
    CpuArch arch;
    CpuModel model;
    OperatingSystem os;
};

BUSTER_LOCAL String target_to_string_builder(Target target)
{
    switch (target.arch)
    {
        break; case CPU_ARCH_X86_64:
        {
            switch (target.model)
            {
                break; case CPU_MODEL_GENERIC:
                {
                    switch (target.os)
                    {
                        break; case OPERATING_SYSTEM_LINUX: return S("x86_64-linux-baseline");
                        break; case OPERATING_SYSTEM_MACOS: return S("x86_64-macos-baseline");
                        break; case OPERATING_SYSTEM_WINDOWS: return S("x86_64-windows-baseline");
                        break; case OPERATING_SYSTEM_UEFI: return S("x86_64-uefi-baseline");
                        break; case OPERATING_SYSTEM_FREESTANDING: return S("x86_64-freestanding-baseline");
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                break; case CPU_MODEL_NATIVE:
                {
                    switch (target.os)
                    {
                        break; case OPERATING_SYSTEM_LINUX: return S("x86_64-linux-native");
                        break; case OPERATING_SYSTEM_MACOS: return S("x86_64-macos-native");
                        break; case OPERATING_SYSTEM_WINDOWS: return S("x86_64-windows-native");
                        break; case OPERATING_SYSTEM_UEFI: return S("x86_64-uefi-native");
                        break; case OPERATING_SYSTEM_FREESTANDING: return S("x86_64-freestanding-native");
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                break; default: BUSTER_UNREACHABLE();
            }
        }
        break; default: BUSTER_UNREACHABLE();
    }
}

STRUCT(CompilationUnit)
{
    Target target;
    CompilationModel model;
    char** compilation_arguments;
    bool debug_info;
    bool io_ring;
    u64 file;
    String object_path;
    Process process;
    u8 cache_padding[BUSTER_MIN(BUSTER_CACHE_LINE_GUESS - ((3 * sizeof(u64))), BUSTER_CACHE_LINE_GUESS)];
};

static_assert(sizeof(CompilationUnit) % BUSTER_CACHE_LINE_GUESS == 0);

STRUCT(LinkUnit)
{
    Process link_process;
    Process run_process;
    String artifact_path;
    Target target;
    u64* compilations;
    u64 compilation_count;
    bool io_ring;
    bool run;
};

BUSTER_LOCAL void append_string(Arena* arena, String s)
{
    let allocation = arena_allocate_bytes(arena, s.length, 1);
    memcpy(allocation, s.pointer, s.length);
}

BUSTER_LOCAL bool target_equal(Target a, Target b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

BUSTER_LOCAL bool build_compile_commands(Arena* arena, Arena* compile_commands, CompilationUnit* units, u64 unit_count, String cwd)
{
    bool result = true;

    let compile_commands_start = compile_commands->position;
    append_string(compile_commands, S("[\n"));

    constexpr u64 max_target_count = 16;
    Target targets[max_target_count];
    u64 target_count = 0;

    for (u64 unit_index = 0; unit_index < unit_count; unit_index += 1)
    {
        CompilationUnit* unit = &units[unit_index];
        let file_index = unit->file;
        let path = file_relative_paths[file_index];

        let dot = str_last_ch(path, '.');
        BUSTER_CHECK(dot != string_no_match);
        let extension_start = dot + 1;
        let extension = (String){ .pointer = path.pointer + extension_start, .length = path.length - extension_start };
        BUSTER_CHECK(str_equal(extension, S("c")));

        {
            let target_string_builder = target_to_string_builder(unit->target);
            String artifact_paths[] = {
                cwd,
                S("/build/"),
                target_string_builder,
                S("/"),
                path,
                S(".o"),
            };

            u64 i;
            for (i = 0; i < target_count; i += 1)
            {
                if (target_equal(unit->target, targets[i]))
                {
                    break;
                }
            }

            char buffer[PATH_MAX];
            u64 count = 0;
            {
                memcpy(buffer + count, artifact_paths[1].pointer + 1, artifact_paths[1].length - 1);
                BUSTER_CHECK(artifact_paths[1].pointer[0] == '/');
                count += artifact_paths[1].length - 1;
            }

            {
                memcpy(buffer + count, artifact_paths[2].pointer, artifact_paths[2].length);
                count += artifact_paths[2].length;
            }

            buffer[count] = 0;

            if (i == target_count)
            {
                mkdir(buffer, 0755);
                targets[target_count] = unit->target;
                target_count += 1;
            }

            buffer[count] = '/';
            count += 1;

            let source_path = artifact_paths[4];
            u64 source_i = 0;
            while (1)
            {
                String source = str_slice_start(source_path, source_i);
                let slash_index = str_first_ch(source, '/');
                if (slash_index == string_no_match)
                {
                    break;
                }

                memcpy(buffer + count, source_path.pointer, slash_index);
                buffer[count + slash_index] = 0;

                mkdir(buffer, 0755);

                source_i = slash_index + 1; 
            }

            let object_path = arena_join_string(arena, BUSTER_STRING_ARRAY_TO_SLICE(artifact_paths), true);
            unit->object_path = object_path;

            String compilation_source_parts[] = {
                cwd,
                S("/"),
                path,
            };

            let compilation_source = arena_join_string(arena, BUSTER_STRING_ARRAY_TO_SLICE(compilation_source_parts), true);
            let clang_env = getenv("CLANG");
            char* clang_path = clang_env ? clang_env : "/usr/bin/clang";

            let builder = argument_builder_start(arena, clang_path);
            // argument_add(builder, "-ferror-limit=1");
            argument_add(builder, "-c");
            argument_add(builder, (char*)compilation_source.pointer);
            argument_add(builder, "-o");
            argument_add(builder, (char*)object_path.pointer);
            argument_add(builder, "-std=gnu2x");
            argument_add(builder, "-Isrc");
            argument_add(builder, "-Wno-pragma-once-outside-header");

            switch (unit->target.model)
            {
                break; case CPU_MODEL_NATIVE: argument_add(builder, "-march=native");
                break; case CPU_MODEL_GENERIC: {}
            }

            if (unit->debug_info)
            {
                argument_add(builder, "-g");
            }

            argument_add(builder, unit->model == COMPILATION_MODEL_SINGLE_UNIT ? "-DBUSTER_UNITY_BUILD=1" : "-DBUSTER_UNITY_BUILD=0");

            argument_add(builder, unit->io_ring ? "-DBUSTER_USE_IO_RING=1" : "-DBUSTER_USE_IO_RING=0");

            let args = argument_builder_end(builder);

            unit->compilation_arguments = args;

            append_string(compile_commands, S("\t{\n\t\t\"directory\": \""));
            append_string(compile_commands, cwd);
            append_string(compile_commands, S("\",\n\t\t\"command\": \""));

            for (char** a = args; *a; a += 1)
            {
                let arg_ptr = *a;
                let arg_len = strlen(arg_ptr);
                let arg = (String){ (u8*)arg_ptr, arg_len };
                append_string(compile_commands, arg);
                append_string(compile_commands, S(" "));
            }

            compile_commands->position -= 1;
            append_string(compile_commands, S("\"\n\t\t\"file\": \""));
            append_string(compile_commands, compilation_source);
            append_string(compile_commands, S("\"\n"));
            append_string(compile_commands, S("\t},\n"));
        }
    }

    compile_commands->position -= 2;

    append_string(compile_commands, S("\n]"));

    let compile_commands_str = (String){ .pointer = (u8*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };

    if (result)
    {
        result = file_write(S("build/compile_commands.json"), compile_commands_str);
        if (!result)
        {
            perror(get_last_error_message());
        }
    }

    return result;
}

BUSTER_LOCAL bool compile(CompilationUnit* units, u64 unit_count, char* envp[])
{
    bool result = true;

    for (u64 unit_index = 0; unit_index < unit_count; unit_index += 1)
    {
        CompilationUnit* unit = &units[unit_index];
        spawn_process(&unit->process, unit->compilation_arguments, envp);
        if (unit->process.pid == -1)
        {
            result = false;
        }
#if BUSTER_USE_IO_RING
        else
        {
            io_ring_prepare_waitid(unit->process.pid, &unit->process.siginfo, (u64)unit);
        }
#endif
    }

#if BUSTER_USE_IO_RING
    let wait_entries = io_ring_submit_and_wait_all();
    result = wait_entries != 0;
#endif

    if (result)
    {
        BUSTER_CHECK(wait_entries == unit_count);

        for (u64 unit_index = 0; unit_index < unit_count; unit_index += 1)
        {
#if BUSTER_USE_IO_RING
            let completion = io_ring_peek_completion();
            let unit = (CompilationUnit*)completion.user_data;
            let wait_result = completion.result;
            bool success = (wait_result == 0) & (unit->process.siginfo.si_code == CLD_EXITED) & (unit->process.siginfo.si_status == 0);
            if (!success)
            {
                // TODO: log failure
                result = false;
            }
#else
            let unit = &units[unit_index];
            let pid = unit->process.pid;

            let success = process_wait_sync(pid, &unit->process.siginfo) == PROCESS_RESULT_SUCCESS;
            if (!success)
            {
                result = false;
            }
#endif
            if ((!success) & global_program.verbose) printf("Compilation failed for artifact %s\n", unit->object_path.pointer);
        }
    }

    return result;
}

BUSTER_LOCAL void queue_linkage_job(Arena* arena, LinkUnit* link_unit, CompilationUnit* units, char* envp[])
{
    let clang_env = getenv("CLANG");
    char* clang_path = clang_env ? clang_env : "/usr/bin/clang";

    let builder = argument_builder_start(arena, clang_path);

    argument_add(builder, "-fuse-ld=lld");
    argument_add(builder, "-o");
    argument_add(builder, (char*)link_unit->artifact_path.pointer);

    for (u64 i = 0; i < link_unit->compilation_count; i += 1)
    {
        let unit_i = link_unit->compilations[i];
        let unit = &units[unit_i];
        let artifact_path = unit->object_path;
        argument_add(builder, (char*)artifact_path.pointer);
    }

    if (link_unit->io_ring)
    {
        argument_add(builder, "-luring");
    }

    let argv = argument_builder_end(builder);
    bool debug = false;
    if (debug)
    {
        let a = argv;
        while (*a)
        {
            printf("%s ", *a);
            a += 1;
        }
        printf("\n");
    }

    spawn_process(&link_unit->link_process, argv, envp);
}

BUSTER_LOCAL ProcessResult process_arguments(Arena* arena, void* context, u64 argc, char** argv, char** envp)
{
    let input = (ProgramInput*)context;
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(envp);

    ProcessResult result = PROCESS_RESULT_SUCCESS;

    {
        let arg_ptr = argv + 1;
        let arg_top = argv + argc;

        if (arg_ptr != arg_top)
        {
            let a = *arg_ptr;
            arg_ptr += 1;

            if (strcmp(a, "test") == 0)
            {
                input->command = BUILD_COMMAND_TEST;
            }
            else if (strcmp(a, "debug") == 0)
            {
                input->command = BUILD_COMMAND_DEBUG;
            }
            else
            {
                result = argument_process(argc, argv, envp, 1);

                if (result != PROCESS_RESULT_SUCCESS)
                {
                    printf("Command '%s' not recognized\n", a);
                }
            }
        }

        if (result == PROCESS_RESULT_SUCCESS)
        {
            while (arg_ptr != arg_top)
            {
                printf("Arguments > 2 not supported\n");
                result = PROCESS_RESULT_FAILED;
                break;
            }
        }
    }

    if (!global_program.verbose & (input->command != BUILD_COMMAND_BUILD))
    {
        global_program.verbose = true;
    }

    return result;
}

BUSTER_LOCAL ProcessResult thread_entry_point(Thread* thread)
{
    BUSTER_UNUSED(thread);
    let arena_init_start = take_timestamp();
    let arena = arena_create((ArenaInitialization){});
    let compile_commands = arena_create((ArenaInitialization){});
    let arena_init_end = take_timestamp();
    let input = (ProgramInput*)thread->context;

    let open_start = take_timestamp();

    ProcessResult result = io_ring_init(&thread->ring, 4096) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

    if (result == PROCESS_RESULT_SUCCESS)
    {
        u64 entry_count = BUSTER_ARRAY_LENGTH(work_files);

        for (u64 i = 0; i < entry_count; i += 1)
        {
            let work_file = &work_files[i];
            let path = file_relative_paths[i];
            work_file_open(work_file, path, i);
        }

        result = io_ring_submit_and_wait_all() ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        // result = ((submission_result >= 0) & ((u64)submission_result == entry_count)) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        let open_end = take_timestamp();

        if (result == PROCESS_RESULT_SUCCESS)
        {
            u64 completed_open_count = 0;
            bool cache_missing = false;

            let stat_start = open_end;

            for (u64 i = 0; i < entry_count; i += 1)
            {
                let completion = io_ring_peek_completion();
                    let i = completion.user_data;
                    let fd = completion.result;

                    let work_file = &work_files[i];
                    work_file->fd = fd;

                    bool is_valid_file_descriptor = fd >= 0;
                    if (!is_valid_file_descriptor & (i == 0))
                    {
                        cache_missing = 1;
                    }

                    if (is_valid_file_descriptor)
                    {
                        io_ring_prepare_stat(fd, &work_file->statx, i, (StatOptions) { .size = 1, .modified_time = 1 });
                        completed_open_count += 1;
                    }
            }

            if (result == PROCESS_RESULT_SUCCESS)
            {
                result = io_ring_submit_and_wait_all() ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                let stat_end = take_timestamp();

                if (result == PROCESS_RESULT_SUCCESS)
                {
                    u64 arena_offset = 0x1000;

                    for (u64 i = 0; i < completed_open_count; i += 1)
                    {
                        let completion = io_ring_peek_completion();
                        let i = completion.user_data;
                        let return_code = completion.result;

                        bool succeeded = return_code == 0;

                        if (succeeded)
                        {
                            let work_file = &work_files[i];
                            let size = work_file->statx.stx_size;
                            let s = work_file->statx.stx_mtime.tv_sec;
                            let ns = work_file->statx.stx_mtime.tv_nsec;
                            work_file->time_s = s;
                            work_file->time_ns = ns;
                            work_file->size = size;

                            work_file->arena_offset = arena_offset;
                            arena_offset += align_forward(size + (64 * 4), 0x1000);
                            work_file->status = WORK_FILE_STATED;
                        }
                        else
                        {
                            printf("Internal failure: statx failed\n");
                            result = PROCESS_RESULT_FAILED;
                            break;
                        }
                    }

                    if (result == PROCESS_RESULT_SUCCESS)
                    {
                        let file_processing_start = take_timestamp();
                        let allocation = arena_allocate_bytes(arena, arena_offset, 0x1000);

                        for (u64 i = 0; i < entry_count; i += 1)
                        {
                            let work_file = &work_files[i];

                            if (work_file->status == WORK_FILE_STATED)
                            {
                                let fd = work_file->fd;

                                {
                                    let arena_offset = work_file->arena_offset;
                                    let pointer = (u8*)allocation + arena_offset;
                                    let size = work_file->size;

                                    work_file->status = WORK_FILE_READING;
                                    io_ring_prepare_read_and_close(fd, pointer, size, i, 0, (u64)1 << 32);
                                }
                            }
                        }

                        u32 submitted_count = io_ring_submit();
                        result = submitted_count ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                        if (result == PROCESS_RESULT_SUCCESS)
                        {
                            while (submitted_count--)
                            {
                                let completion = io_ring_wait_completion();

                                let user_data = completion.user_data;
                                bool is_read = (user_data >> 32) == 0;
                                let index = user_data & UINT32_MAX;
                                let return_value = completion.result;

                                if (is_read)
                                {
                                    let work_file = &work_files[index];
                                    let file_size = work_file->size;

                                    if ((return_value >= 0) & ((u64)return_value != file_size))
                                    {
                                        printf("File sizes don't match");
                                        result = PROCESS_RESULT_FAILED;
                                        break;
                                    }

                                    let arena_offset = work_file->arena_offset;
                                    let hash = hash_file((const char*)allocation + arena_offset, work_file->size);
                                    work_file->hash = hash;
                                    work_file->status = WORK_FILE_READ;
                                }
                                else
                                {
                                    if (return_value != 0)
                                    {
                                        printf("Close failed\n");
                                        result = PROCESS_RESULT_SUCCESS;
                                        break;
                                    }
                                }
                            }

                            let file_processing_end = take_timestamp();
                            let compile_command_start = file_processing_end;

                            if (result == PROCESS_RESULT_SUCCESS)
                            {
                                if (cache_missing)
                                {
                                    u64 left_out_compilation_count = 1;

                                    let candidate_compilation_unit_count = BUSTER_ARRAY_LENGTH(work_files) - left_out_compilation_count;
                                    u64 compilation_unit_count = 0;

                                    for (u64 i = 0; i < candidate_compilation_unit_count; i += 1)
                                    {
                                        let file_index = i + left_out_compilation_count;
                                        let path = file_relative_paths[file_index];

                                        let dot = str_last_ch(path, '.');

                                        if (dot != string_no_match)
                                        {
                                            let extension_start = dot + 1;
                                            let extension = (String){ .pointer = path.pointer + extension_start, .length = path.length - extension_start };

                                            if (str_equal(extension, S("c")))
                                            {
                                                compilation_unit_count += 1;
                                            }
                                        }
                                    }

                                    let compilation_units = arena_allocate(arena, CompilationUnit, compilation_unit_count);
                                    u64 compilation_unit_i = 0;

                                    CompilationModel default_compilation_model = COMPILATION_MODEL_INCREMENTAL;
                                    CpuModel default_cpu_model = CPU_MODEL_GENERIC;

                                    for (u64 i = 0; i < candidate_compilation_unit_count; i += 1)
                                    {
                                        let file_index = i + left_out_compilation_count;
                                        let path = file_relative_paths[file_index];

                                        let dot = str_last_ch(path, '.');

                                        if (dot != string_no_match)
                                        {
                                            let extension_start = dot + 1;
                                            let extension = (String){ .pointer = path.pointer + extension_start, .length = path.length - extension_start };

                                            if (str_equal(extension, S("c")))
                                            {
                                                let compilation_unit = &compilation_units[compilation_unit_i];
                                                compilation_unit_i += 1;

                                                *compilation_unit = (CompilationUnit) {
                                                    .file = file_index,
                                                    .model = i == 0 ? COMPILATION_MODEL_SINGLE_UNIT : default_compilation_model,
                                                    .target = {
                                                        .model = i == 0 ? CPU_MODEL_NATIVE : default_cpu_model,
                                                    },
                                                    .debug_info = true,
                                                    .io_ring = i == 0,
                                                };
                                            }
                                        }
                                    }

                                    BUSTER_CHECK(compilation_unit_i == compilation_unit_count);

                                    let cwd = path_absolute(arena, ".");

                                    result = build_compile_commands(arena, compile_commands, compilation_units, compilation_unit_count, cwd) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                                    let compile_command_end = take_timestamp();

                                    if (result == PROCESS_RESULT_SUCCESS)
                                    {
                                        let compile_start = compile_command_end;
                                        result = compile(compilation_units, compilation_unit_count, global_program.envp) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
                                        let compile_end = take_timestamp();

                                        if (result == PROCESS_RESULT_SUCCESS)
                                        {
                                            let link_start = take_timestamp();

                                            u64 builder_indices[] = { 0, 1 };
                                            let disk_indices = arena_allocate(arena, u64, compilation_unit_count - 1);
                                            for (u64 i = 0; i < compilation_unit_count; i += 1)
                                            {
                                                disk_indices[i] = i + 1;
                                            }
                                            LinkUnit link_units[] = {
                                                {
                                                    .artifact_path = S("build/builder"),
                                                    .compilations = builder_indices,
                                                    .compilation_count = BUSTER_ARRAY_LENGTH(builder_indices),
                                                    .io_ring = true,
                                                },
                                                {
                                                    .artifact_path = S("build/disk_builder"),
                                                    .compilations = disk_indices,
                                                    .compilation_count = compilation_unit_count - 1,
                                                    .run = true,
                                                },
                                            };


                                            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(link_units); i += 1)
                                            {
                                                LinkUnit* link_unit = &link_units[i];
                                                queue_linkage_job(arena, link_unit, compilation_units, global_program.envp);
                                            }

                                            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(link_units); i += 1)
                                            {
                                                let link_unit = &link_units[i];
                                                let link_process = &link_unit->link_process;
                                                io_ring_prepare_waitid(link_process->pid, &link_process->siginfo, (u64)link_process | TASK_ID_LINKING);
                                            }

                                            result = io_ring_submit_and_wait_all() ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
                                            if (result != PROCESS_RESULT_SUCCESS)
                                            {
                                                if (global_program.verbose) printf("Waiting for linker jobs failed!\n");
                                            }

                                            let link_end = take_timestamp();

                                            if (global_program.verbose && 0)
                                            {
                                                printf("Arena: %lu ns\n", ns_between(arena_init_start, arena_init_end));
                                                //printf("Thread: %lu ns\n", ns_between(thread_start, thread_end));
                                                printf("Open: %lu ns\n", ns_between(open_start, open_end));
                                                printf("Stat: %lu ns\n", ns_between(stat_start, stat_end));
                                                printf("File processing: %lu ns\n", ns_between(file_processing_start, file_processing_end));
                                                printf("Compile command building: %lu ns\n", ns_between(compile_command_start, compile_command_end));
                                                printf("Compilation (%lu objects): %lu ns\n", compilation_unit_count, ns_between(compile_start, compile_end));
                                                printf("Link: %lu ns\n", ns_between(link_start, link_end));
                                            }

                                            if (result == PROCESS_RESULT_SUCCESS)
                                            {
                                                switch (input->command)
                                                {
                                                    break; case BUILD_COMMAND_BUILD:
                                                    {
                                                    }
                                                    break; case BUILD_COMMAND_TEST:
                                                    {
                                                        let link_unit = &link_units[1];

                                                        char* args[] = {
                                                            (char*)link_unit->artifact_path.pointer,
                                                            0,
                                                        };
                                                        spawn_process(&link_unit->run_process, args, global_program.envp);
                                                        result = process_wait_sync(link_unit->run_process.pid, &link_unit->run_process.siginfo);
                                                        if (result == PROCESS_RESULT_SUCCESS)
                                                        {
                                                            printf("All tests succeeded!\n");
                                                        }
                                                    }
                                                    break; case BUILD_COMMAND_DEBUG:
                                                    {
                                                        let link_unit = &link_units[1];

                                                        char* args[] = {
                                                            "/usr/bin/gdb",
                                                            "-ex",
                                                            "r",
                                                            "--args",
                                                            (char*)link_unit->artifact_path.pointer,
                                                            0,
                                                        };
                                                        spawn_process(&link_unit->run_process, args, global_program.envp);
                                                        result = process_wait_sync(link_unit->run_process.pid, &link_unit->run_process.siginfo);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    BUSTER_TRAP();
                                }
                            }
                        }
                        else
                        {
                            // TODO:
                            printf("Unmatched entry count\n");
                        }
                    }
                }
            }
        }
    }

    return result;
}

int main(int argc, char* argv[], char* envp[])
{
    ProgramInput input = {};
    return buster_run((BusterInitialization){
        .process_arguments = &process_arguments,
        .thread_entry_point = &thread_entry_point,
        .context = &input,
        .argv = argv,
        .envp = envp,
        .argc = argc,
        .thread_spawn_policy = THREAD_SPAWN_POLICY_SINGLE_THREADED,
    });
}
