#if 0
#!/usr/bin/env bash
set -eux
if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
    export CLANG=/usr/bin/clang
else
    export CLANG=$CMAKE_PREFIX_PATH/bin/clang
fi
#if 0BUSTER_REGENERATE=0 build/builder $@ 2>/dev/null
#endif
#if [[ "$?" != "0" && "$?" != "333" ]]; then
    mkdir -p build
    $CLANG build.c -o build/builder -Isrc -std=gnu2x -march=native -luring -DUNITY_BUILD=1 -g
    if [[ "$?" == "0" ]]; then
        BUSTER_REGENERATE=1 build/builder $@
    fi
#endif fi
exit $?
#endif

#include <lib.h>

#if UNITY_BUILD
#include <lib.c>
#endif
#include <liburing.h>
#include <meow_hash_x64_aesni.h>
#include <stdio.h>

STRUCT(ArgumentBuilder)
{
    char** argv;
    Arena* arena;
    u64 arena_offset;
};

LOCAL char** argument_add(ArgumentBuilder* builder, char* arg)
{
    let ptr = arena_allocate(builder->arena, char*, 1);
    *ptr = arg;
    return ptr;
}

LOCAL ArgumentBuilder* argument_builder_start(Arena* arena, char* s)
{
    let position = arena->position;
    let argument_builder = arena_allocate(arena, ArgumentBuilder, 1);
    *argument_builder = (ArgumentBuilder) {
        .argv = 0,
        .arena = arena,
        .arena_offset = position,
    };
    argument_builder->argv = argument_add(argument_builder, s);
    return argument_builder;
}

LOCAL char** argument_builder_end(ArgumentBuilder* restrict builder)
{
    argument_add(builder, 0);
    return builder->argv;
}

LOCAL void argument_builder_destroy(ArgumentBuilder* restrict builder)
{
    let arena = builder->arena;
    let position = builder->arena_offset;
    arena->position = position;
}

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
    str artifact_path;
    struct statx statx;
    u64 time_s;
    u64 time_ns;
    u64 size;
    u64 arena_offset;
    int fd;
    WorkFileStatus status;
    u8 cache_padding[MIN(CACHE_LINE_GUESS - ((1 * sizeof(u64))), CACHE_LINE_GUESS)];
};
static_assert(sizeof(WorkFile) % CACHE_LINE_GUESS == 0);

STRUCT(OldFile)
{
    u8 cache_padding[CACHE_LINE_GUESS];
};

static_assert(sizeof(OldFile) % CACHE_LINE_GUESS == 0);

STRUCT(Artifact)
{
    str path;
    Files file;
};

LOCAL str file_relative_paths[] = {
    S("build/cache_manifest"),
    S("build.c"),
    S("src/lib.h"),
    S("src/lib.c"),
    S("src/meow_hash_x64_aesni.h"),
    S("src/disk_builder.c"),
};

LOCAL WorkFile work_files[array_length(file_relative_paths)] = {};

LOCAL constexpr u32 io_uring_queue_entry_count = 4096;

LOCAL void work_file_open(WorkFile* file, str path, struct io_uring_sqe* sqe)
{
    io_uring_prep_openat(sqe, AT_FDCWD, path.pointer, O_RDONLY, 0);
    file->status = WORK_FILE_OPENING;
    file->fd = -1;
}

LOCAL u128 hash_file(const char* pointer, u64 length)
{
    check(((u64)pointer & (64 - 1)) == 0);

    let result = MeowHash(MeowDefaultSeed, length, (void*)pointer);
    return *(u128*)&result;
}

typedef enum ProcessResult
{
    PROCESS_RESULT_SUCCESS,
    PROCESS_RESULT_FAILED,
    PROCESS_RESULT_FAILED_TRY_AGAIN,
    PROCESS_RESULT_CRASH,
    PROCESS_RESULT_UNKNOWN,
} ProcessResult;

LOCAL pid_t spawn_process(char* argv[], char* envp[])
{
    let pid = fork();

    if (pid == -1)
    {
        printf("Failed to fork\n");
    }
    else if (pid == 0)
    {
        execve(argv[0], argv, envp);
        trap();
    }

    return pid;
}

LOCAL ProcessResult wait_for_process(pid_t pid)
{
    int status;
    let wait_result = waitpid(pid, &status, 0);

    ProcessResult result;
    if (wait_result == pid)
    {
        if (WIFEXITED(status))
        {
            let exit_status = WEXITSTATUS(status);
            result = (ProcessResult)exit_status;
            check(result <= PROCESS_RESULT_FAILED_TRY_AGAIN);
        }
        else
        {
            result = PROCESS_RESULT_CRASH;
        }
    }
    else
    {
        printf("Wait failed\n");
        result = PROCESS_RESULT_UNKNOWN;
    }

    return result;
}

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

STRUCT(CompilationUnit)
{
    Target target;
    CompilationModel model;
    char** compilation_arguments;
    bool debug_info;
    u64 file;
    pid_t pid;
    u8 cache_padding[MIN(CACHE_LINE_GUESS - ((6 * sizeof(u64))), CACHE_LINE_GUESS)];
};

static_assert(sizeof(CompilationUnit) % CACHE_LINE_GUESS == 0);

LOCAL void append_string(Arena* arena, str s)
{
    let allocation = arena_allocate_bytes(arena, s.length, 1);
    memcpy(allocation, s.pointer, s.length);
}

LOCAL bool build_compile_commands(Arena* arena, Arena* compile_commands, CompilationUnit* units, u64 unit_count, char* envp[], str cwd)
{
    bool result = true;

    let compile_commands_start = compile_commands->position;
    append_string(compile_commands, S("[\n"));

    for (u64 unit_index = 0; unit_index < unit_count; unit_index += 1)
    {
        CompilationUnit* unit = &units[unit_index];
        let file_index = unit->file;
        let work_file = &work_files[file_index];
        let path = file_relative_paths[file_index];

        let dot = str_last_ch(path, '.');
        check(dot != string_no_match);
        let extension_start = dot + 1;
        let extension = (str){ .pointer = path.pointer + extension_start, .length = path.length - extension_start };
        check(str_equal(extension, S("c")));

        {
            str artifact_paths[] = {
                cwd,
                S("/build/"),
                path,
                S(".o"),
            };
            let artifact_path = arena_join_string(arena, string_array_to_slice(artifact_paths), true);
            work_file->artifact_path = artifact_path;

            let compilation_model = COMPILATION_MODEL_INCREMENTAL;
            str compilation_source_parts[] = {
                cwd,
                S("/"),
                path,
            };

            let compilation_source = arena_join_string(arena, string_array_to_slice(compilation_source_parts), true);
            let clang_env = getenv("CLANG");
            char* clang_path = clang_env ? clang_env : "/usr/bin/clang";
            char* model_arg;
            switch (unit->target.model)
            {
                break; case CPU_MODEL_GENERIC:
                    switch (unit->target.arch)
                    {
                        break; case CPU_ARCH_X86_64: model_arg = "-march=x86-64";
                        break; default: UNREACHABLE();
                    }
                break; case CPU_MODEL_NATIVE: model_arg = "-march=native";
                break; default: UNREACHABLE();
            }

            let builder = argument_builder_start(arena, clang_path);
            argument_add(builder, "-c");
            argument_add(builder, compilation_source.pointer);
            argument_add(builder, "-o");
            argument_add(builder, artifact_path.pointer);
            argument_add(builder, "-std=gnu2x");
            argument_add(builder, "-Isrc");
            argument_add(builder, "-Wno-pragma-once-outside-header");

            if (unit->target.model == CPU_MODEL_NATIVE)
            {
                argument_add(builder, "-march=native");
            }

            if (unit->debug_info)
            {
                argument_add(builder, "-g");
            }

            argument_add(builder, unit->model == COMPILATION_MODEL_SINGLE_UNIT ? "-DUNITY_BUILD=1" : "-DUNITY_BUILD=0");

            let args = argument_builder_end(builder);

            unit->compilation_arguments = args;

            append_string(compile_commands, S("\t{\n\t\t\"directory\": \""));
            append_string(compile_commands, cwd);
            append_string(compile_commands, S("\",\n\t\t\"command\": \""));

            for (char** a = args; *a; a += 1)
            {
                let arg_ptr = *a;
                let arg_len = strlen(arg_ptr);
                let arg = (str){ arg_ptr, arg_len };
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

    let compile_commands_str = (str){ .pointer = (char*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };

    if (result)
    {
        result = file_write(S("build/compile_commands.json"), compile_commands_str);
        if (!result)
        {
            let e = errno;
            perror(strerror(e));
        }
    }

    return result;
}

LOCAL bool compile(CompilationUnit* units, u64 unit_count, char* envp[])
{
    bool result = true;

    for (u64 unit_index = 0; unit_index < unit_count; unit_index += 1)
    {
        CompilationUnit* unit = &units[unit_index];
        let pid = spawn_process(unit->compilation_arguments, envp);
        unit->pid = pid;
        if (pid == -1)
        {
            result = false;
        }
    }

    for (u64 unit_index = 0; unit_index < unit_count; unit_index += 1)
    {
        let unit = &units[unit_index];
        let pid = unit->pid;
        let file_index = unit->file;
        let work_file = &work_files[file_index];

        int status = 0;
        let success = wait_for_process(pid) == PROCESS_RESULT_SUCCESS;
        if (!success)
        {
            printf("Compilation failed for artifact %s\n", work_file->artifact_path.pointer);
            result = false;
        }
    }

    return result;
}

STRUCT(LinkOptions)
{
    str artifact_output;
    bool io_uring;
};

LOCAL bool perform_linkage(Arena* arena, CompilationUnit* units, u64* unit_indices, u64 unit_count, char* envp[], LinkOptions options)
{
    bool result = false;

    let first_arg = arena_allocate(arena, char*, 1);

    let clang_env = getenv("CLANG");
    char* clang_path = clang_env ? clang_env : "/usr/bin/clang";

    let builder = argument_builder_start(arena, clang_path);

    argument_add(builder, "-fuse-ld=lld");
    argument_add(builder, "-o");
    argument_add(builder, options.artifact_output.pointer);

    for (u64 i = 0; i < unit_count; i += 1)
    {
        let unit_i = unit_indices[i];
        let unit = &units[unit_i];
        let unit_file = &work_files[unit->file];
        let artifact_path = unit_file->artifact_path;
        argument_add(builder, artifact_path.pointer);
    }

    if (options.io_uring)
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

    let pid = spawn_process(argv, envp);
    if (pid != -1)
    {
        result = wait_for_process(pid) == PROCESS_RESULT_SUCCESS;
    }
    argument_builder_destroy(builder);

    return result;
}

LOCAL ProcessResult main_function(int argc, char* argv[], char* envp[])
{
    let arena = arena_create((ArenaInitialization){});
    let compile_commands = arena_create((ArenaInitialization){});

    struct io_uring r;
    let ring = &r;
    ProcessResult result = io_uring_queue_init(io_uring_queue_entry_count, ring, 0);
    if (result == PROCESS_RESULT_SUCCESS)
    {
        u64 entry_count = array_length(work_files);

        for (u64 i = 0; i < entry_count; i += 1)
        {
            struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
            sqe->user_data = i;

            let work_file = &work_files[i];
            let path = file_relative_paths[i];
            work_file_open(work_file, path, sqe);
        }

        int submission_result = io_uring_submit_and_wait(ring, entry_count);
        result = submission_result == entry_count ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

        if (result == PROCESS_RESULT_SUCCESS)
        {
            u64 completed_open_count = 0;
            bool cache_missing = false;

            for (u64 i = 0; i < entry_count; i += 1)
            {
                struct io_uring_cqe* cqe;
                
                if (io_uring_peek_cqe(ring, &cqe) == 0)
                {
                    let i = cqe->user_data;
                    int fd = cqe->res;
                    io_uring_cqe_seen(ring, cqe);

                    let work_file = &work_files[i];
                    work_file->fd = fd;

                    bool is_valid_file_descriptor = fd >= 0;
                    if (!is_valid_file_descriptor & (i == 0))
                    {
                        cache_missing = 1;
                    }

                    if (is_valid_file_descriptor)
                    {
                        let sqe = io_uring_get_sqe(ring);
                        sqe->user_data = i;
                        io_uring_prep_statx(sqe, fd, "", AT_EMPTY_PATH, STATX_MTIME | STATX_SIZE, &work_file->statx);
                        completed_open_count += 1;
                    }
                }
                else
                {
                    printf("Internal failure: openat\n");
                    result = PROCESS_RESULT_FAILED;
                    break;
                }
            }

            if (result == PROCESS_RESULT_SUCCESS)
            {
                let statx_entry_count = entry_count - (entry_count - completed_open_count);

                submission_result = io_uring_submit_and_wait(ring, statx_entry_count);

                result = submission_result == statx_entry_count ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                if (result == PROCESS_RESULT_SUCCESS)
                {
                    u64 arena_offset = 0x1000;

                    for (int i = 0; i < submission_result; i += 1)
                    {
                        struct io_uring_cqe* cqe;
                        if (io_uring_peek_cqe(ring, &cqe) == 0)
                        {
                            let i = cqe->user_data;
                            int return_code = cqe->res;
                            io_uring_cqe_seen(ring, cqe);

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
                        else
                        {
                            printf("Internal failure: statx\n");
                            result = PROCESS_RESULT_FAILED;
                            break;
                        }
                    }

                    if (result == PROCESS_RESULT_SUCCESS)
                    {
                        let allocation = arena_allocate_bytes(arena, arena_offset, 0x1000);

                        for (u64 i = 0; i < entry_count; i += 1)
                        {
                            let work_file = &work_files[i];

                            if (work_file->status == WORK_FILE_STATED)
                            {
                                let fd = work_file->fd;

                                {
                                    let arena_offset = work_file->arena_offset;
                                    let pointer = allocation + arena_offset;
                                    let size = work_file->size;

                                    let sqe = io_uring_get_sqe(ring);
                                    sqe->user_data = i;
                                    io_uring_prep_read(sqe, fd, pointer, size, 0);
                                    work_file->status = WORK_FILE_READING;
                                }

                                {
                                    let sqe = io_uring_get_sqe(ring);
                                    sqe->user_data = ((u64)1 << 32) | i;
                                    io_uring_prep_close(sqe, fd);
                                }
                            }
                        }

                        submission_result = io_uring_submit(ring);
                        let countdown = statx_entry_count * 2;
                        result = submission_result == countdown ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                        if (result == PROCESS_RESULT_SUCCESS)
                        {
                            while (countdown--)
                            {
                                struct io_uring_cqe* cqe;

                                if (io_uring_wait_cqe(ring, &cqe) == 0)
                                {
                                    let user_data = cqe->user_data;
                                    bool is_read = (user_data >> 32) == 0;
                                    let index = user_data & UINT32_MAX;
                                    let return_value = cqe->res;
                                    io_uring_cqe_seen(ring, cqe);

                                    if (is_read)
                                    {
                                        let work_file = &work_files[index];
                                        let file_size = work_file->size;

                                        if (return_value != file_size)
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
                                else
                                {
                                    printf("Wait for cqe failed\n");
                                    result = PROCESS_RESULT_SUCCESS;
                                    break;
                                }
                            }

                            if (result == PROCESS_RESULT_SUCCESS)
                            {
                                if (cache_missing)
                                {
                                    mkdir("build/src", 0755);

                                    u64 left_out_compilation_count = 1;

                                    let candidate_compilation_unit_count = array_length(work_files) - left_out_compilation_count;
                                    u64 compilation_unit_count = 0;

                                    for (u64 i = 0; i < candidate_compilation_unit_count; i += 1)
                                    {
                                        let file_index = i + left_out_compilation_count;
                                        let path = file_relative_paths[file_index];

                                        let dot = str_last_ch(path, '.');

                                        if (dot != string_no_match)
                                        {
                                            let extension_start = dot + 1;
                                            let extension = (str){ .pointer = path.pointer + extension_start, .length = path.length - extension_start };

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
                                            let extension = (str){ .pointer = path.pointer + extension_start, .length = path.length - extension_start };

                                            if (str_equal(extension, S("c")))
                                            {
                                                let compilation_unit = &compilation_units[compilation_unit_i];
                                                compilation_unit_i += 1;

                                                *compilation_unit = (CompilationUnit) {
                                                    .file = file_index,
                                                    .pid = -1,
                                                    .model =  i == 0 ? COMPILATION_MODEL_SINGLE_UNIT : default_compilation_model,
                                                    .target = {
                                                        .model = i == 0 ? CPU_MODEL_NATIVE : default_cpu_model,
                                                    },
                                                    .debug_info = true,
                                                };
                                            }
                                        }
                                    }

                                    check(compilation_unit_i == compilation_unit_count);

                                    let cwd = path_absolute(arena, ".");

                                    result = build_compile_commands(arena, compile_commands, compilation_units, compilation_unit_count, envp, cwd) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                                    if (result == PROCESS_RESULT_SUCCESS)
                                    {
                                        result = compile(compilation_units, compilation_unit_count, envp) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                                        if (result == PROCESS_RESULT_SUCCESS)
                                        {
                                            {
                                                u64 indices[] = { 0, 1 };
                                                let artifact_output = S("build/builder");
                                                LinkOptions options = {
                                                    .artifact_output = artifact_output,
                                                    .io_uring = true,
                                                };
                                                result = perform_linkage(arena, compilation_units, indices, array_length(indices), envp, options) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
                                            }

                                            {
                                                let unit_indices = arena_allocate(arena, u64, compilation_unit_count - 1);
                                                for (u64 i = 0; i < compilation_unit_count; i += 1)
                                                {
                                                    unit_indices[i] = i;
                                                }

                                                let artifact_output = S("build/disk_builder");
                                                LinkOptions options = {
                                                    .artifact_output = artifact_output,
                                                };
                                                result = perform_linkage(arena, compilation_units + 1, unit_indices, compilation_unit_count - 1, envp, options) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                                                if (result == PROCESS_RESULT_SUCCESS)
                                                {
                                                    char* args[] = {
                                                        artifact_output.pointer,
                                                        0,
                                                    };
                                                    let pid = spawn_process(args, envp);
                                                    result = pid != -1 ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
                                                    if (result == PROCESS_RESULT_SUCCESS)
                                                    {
                                                        result = wait_for_process(pid);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    trap();
                                }
                            }
                        }
                        else
                        {
                            printf("Unmatched entry count: expected %lu but got %d\n", countdown, submission_result);
                        }
                    }
                }
            }
        }

        io_uring_queue_exit(ring);
    }

    return result;
}

int main(int argc, char* argv[], char* envp[])
{
    let result = main_function(argc, argv, envp);
    return (int)result;
}
