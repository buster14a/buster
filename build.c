#if 0
#!/usr/bin/env bash
set -eu

if [[ -z "${BUSTER_CI:-}" ]]; then
    BUSTER_CI=0
else
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
    $CLANG build.c -o build/builder -Isrc -std=gnu2x -march=native -luring -DUNITY_BUILD=1 -g -Werror -Wall -Wextra -Wpedantic -pedantic -Wno-gnu-auto-type -Wno-gnu-empty-struct -Wno-bitwise-instead-of-logical -Wno-unused-function -pthread
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
#include <pthread.h>

#define todo() trap()

STRUCT(IoRing)
{
#ifdef __linux__
    struct io_uring linux_impl;
#else
#pragma error
#endif
};

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
    struct statx statx;
    u64 time_s;
    u64 time_ns;
    u64 size;
    u64 arena_offset;
    int fd;
    WorkFileStatus status;
    u8 cache_padding[MIN(CACHE_LINE_GUESS - ((0 * sizeof(u64))), CACHE_LINE_GUESS)];
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
    static_assert(sizeof(result) == sizeof(u128));
    return *(u128*)&result;
}

typedef enum ProcessResult
{
    PROCESS_RESULT_SUCCESS,
    PROCESS_RESULT_FAILED,
    PROCESS_RESULT_FAILED_TRY_AGAIN,
    PROCESS_RESULT_CRASH,
    PROCESS_RESULT_NOT_EXISTENT,
    PROCESS_RESULT_RUNNING,
    PROCESS_RESULT_UNKNOWN,
} ProcessResult;

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
            if (likely(p->siginfo.si_code == CLD_EXITED))
            {
                result = (ProcessResult)p->siginfo.si_status;
            }
            else
            {
                todo();
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

LOCAL void spawn_process(Process* process, char* argv[], char* envp[])
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

LOCAL u32 process_wait_exit_queue(IoRing* ring, Process* process, u8 task_id)
{
    static_assert(alignof(Process) == 8);
    check(((u64)process & 0b111) == 0);
    check((task_id & 0xf8) == 0);
    let pid = process->pid;
    u32 queue_count = 0;

    if (pid != -1)
    {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring->linux_impl);
        sqe->user_data = (u64)process | task_id;
        io_uring_prep_waitid(sqe, P_PID, process->pid, &process->siginfo, WEXITED, 0);
        queue_count += 1;
    }

    return queue_count;
}

LOCAL ProcessResult process_wait_exit_sync(Process* process)
{
    ProcessResult result;
    if (process->pid != -1)
    {
        int wait_result = waitid(P_PID, process->pid, &process->siginfo, WEXITED);

        if (wait_result == 0)
        {
            if (process->siginfo.si_code == CLD_EXITED)
            {
                result = (ProcessResult)process->siginfo.si_status;
            }
            else
            {
                result = PROCESS_RESULT_UNKNOWN;
            }
        }
        else
        {
            result = PROCESS_RESULT_UNKNOWN;
        }
    }
    else
    {
        result = PROCESS_RESULT_NOT_EXISTENT;
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

LOCAL str target_to_string_builder(Target target)
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
                        break; default: UNREACHABLE();
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
                        break; default: UNREACHABLE();
                    }
                }
                break; default: UNREACHABLE();
            }
        }
        break; default: UNREACHABLE();
    }
}

STRUCT(CompilationUnit)
{
    Target target;
    CompilationModel model;
    char** compilation_arguments;
    bool debug_info;
    u64 file;
    str object_path;
    Process process;
    u8 cache_padding[MIN(CACHE_LINE_GUESS - ((3 * sizeof(u64))), CACHE_LINE_GUESS)];
};

static_assert(sizeof(CompilationUnit) % CACHE_LINE_GUESS == 0);

STRUCT(LinkUnit)
{
    Process link_process;
    Process run_process;
    str artifact_path;
    Target target;
    u64* compilations;
    u64 compilation_count;
    bool io_uring;
    bool run;
};

LOCAL void append_string(Arena* arena, str s)
{
    let allocation = arena_allocate_bytes(arena, s.length, 1);
    memcpy(allocation, s.pointer, s.length);
}

LOCAL bool target_equal(Target a, Target b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

LOCAL bool build_compile_commands(Arena* arena, Arena* compile_commands, CompilationUnit* units, u64 unit_count, str cwd)
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
        check(dot != string_no_match);
        let extension_start = dot + 1;
        let extension = (str){ .pointer = path.pointer + extension_start, .length = path.length - extension_start };
        check(str_equal(extension, S("c")));

        {
            let target_string_builder = target_to_string_builder(unit->target);
            str artifact_paths[] = {
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
                check(artifact_paths[1].pointer[0] == '/');
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
                str source = str_slice_start(source_path, source_i);
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

            let object_path = arena_join_string(arena, string_array_to_slice(artifact_paths), true);
            unit->object_path = object_path;

            str compilation_source_parts[] = {
                cwd,
                S("/"),
                path,
            };

            let compilation_source = arena_join_string(arena, string_array_to_slice(compilation_source_parts), true);
            let clang_env = getenv("CLANG");
            char* clang_path = clang_env ? clang_env : "/usr/bin/clang";

            let builder = argument_builder_start(arena, clang_path);
            argument_add(builder, "-c");
            argument_add(builder, compilation_source.pointer);
            argument_add(builder, "-o");
            argument_add(builder, object_path.pointer);
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

LOCAL bool compile(struct io_uring* ring, CompilationUnit* units, u64 unit_count, char* envp[])
{
    bool result = true;

#define USE_IO_URING 1

    for (u64 unit_index = 0; unit_index < unit_count; unit_index += 1)
    {
        CompilationUnit* unit = &units[unit_index];
        spawn_process(&unit->process, unit->compilation_arguments, envp);
        if (unit->process.pid == -1)
        {
            result = false;
        }
#if USE_IO_URING
        else
        {
            struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
            sqe->user_data = (u64)unit;
            io_uring_prep_waitid(sqe, P_PID, unit->process.pid, &unit->process.siginfo, WEXITED, 0);
        }
#endif
    }

#if USE_IO_URING
    int submission_result = io_uring_submit_and_wait(ring, unit_count);
    result = (submission_result >= 0) & ((u64)submission_result == unit_count);
#endif

    if (result)
    {
#if USE_IO_URING
        struct io_uring_cqe* cqe;
        if (io_uring_peek_cqe(ring, &cqe) == 0)
        {
            int wait_result = cqe->res;
            let unit = (CompilationUnit*)cqe->user_data;
            io_uring_cqe_seen(ring, cqe);

            bool execution_result = (wait_result == 0) & (unit->process.siginfo.si_code == CLD_EXITED) & (unit->process.siginfo.si_status == 0);
            if (!execution_result)
            {
                result = false;
            }
        }
        else
        {
            result = false;
        }
#else
        unused(ring);

        for (u64 unit_index = 0; unit_index < unit_count; unit_index += 1)
        {
            let unit = &units[unit_index];
            let pid = unit->pid;

            let success = wait_for_process(pid) == PROCESS_RESULT_SUCCESS;
            if (!success)
            {
                printf("Compilation failed for artifact %s\n", unit->object_path.pointer);
                result = false;
            }
        }
#endif
    }

    return result;
}

LOCAL void queue_linkage_job(Arena* arena, LinkUnit* link_unit, CompilationUnit* units, char* envp[])
{
    let clang_env = getenv("CLANG");
    char* clang_path = clang_env ? clang_env : "/usr/bin/clang";

    let builder = argument_builder_start(arena, clang_path);

    argument_add(builder, "-fuse-ld=lld");
    argument_add(builder, "-o");
    argument_add(builder, link_unit->artifact_path.pointer);

    for (u64 i = 0; i < link_unit->compilation_count; i += 1)
    {
        let unit_i = link_unit->compilations[i];
        let unit = &units[unit_i];
        let artifact_path = unit->object_path;
        argument_add(builder, artifact_path.pointer);
    }

    if (link_unit->io_uring)
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

typedef enum BuildCommand
{
    BUILD_COMMAND_BUILD,
    BUILD_COMMAND_TEST,
} BuildCommand;

#if 0
constexpr u64 thread_count = 16;

LOCAL pthread_t thread_handles[thread_count];
#endif

LOCAL void* dummy_thread(void*)
{
    return (void*)0;
}

STRUCT(Logger)
{
};

STRUCT(Thread)
{
    Logger logger;
};

LOCAL bool io_ring_init(IoRing* ring, u32 entry_count)
{
    bool result = true;
#ifdef __linux__
    int io_uring_queue_creation_result = io_uring_queue_init(entry_count, &ring->linux_impl, 0);
    result &= io_uring_queue_creation_result == 0;
#endif
    return result;
}

LOCAL __thread Thread thread;

LOCAL ProcessResult main_function(int argc, char* argv[], char* envp[])
{
    os_init();

    BuildCommand command = {};

    bool verbose = false;

    {
        let arg_ptr = argv + 1;
        let arg_top = argv + argc;

        if (arg_ptr != arg_top)
        {
            let a = *arg_ptr;
            arg_ptr += 1;

            if (strcmp(a, "test") == 0)
            {
                command = BUILD_COMMAND_TEST;
                verbose = true;
            }
            else
            {
                printf("Command '%s' not recognized\n", a);
                return PROCESS_RESULT_FAILED;
            }
        }

        while (arg_ptr != arg_top)
        {
            printf("Arguments > 2 not supported\n");
            return PROCESS_RESULT_FAILED;
        }
    }

    let arena_init_start = take_timestamp();
    let arena = arena_create((ArenaInitialization){});
    let compile_commands = arena_create((ArenaInitialization){});
    let arena_init_end = take_timestamp();

    let thread_start = arena_init_end;
#if 0
    for (u64 i = 0; i < thread_count; i += 1)
    {
        pthread_create(&thread_handles[i], 0, &dummy_thread, 0);
    }
#endif

    let thread_end = take_timestamp();
    let open_start = thread_end;

    struct io_uring r;
    let ring = &r;

    LOCAL constexpr u32 io_ring_entry_count = 4096;
    ProcessResult result = io_uring_queue_init(io_ring_entry_count, ring, 0);

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
        result = ((submission_result >= 0) & ((u64)submission_result == entry_count)) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        let open_end = take_timestamp();

        if (result == PROCESS_RESULT_SUCCESS)
        {
            u64 completed_open_count = 0;
            bool cache_missing = false;

            let stat_start = open_end;

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

                result = ((submission_result >= 0) & ((u64)submission_result == statx_entry_count)) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
                let stat_end = take_timestamp();

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
                        result = ((submission_result >= 0) & ((u64)submission_result == countdown)) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

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
                                else
                                {
                                    printf("Wait for cqe failed\n");
                                    result = PROCESS_RESULT_SUCCESS;
                                    break;
                                }
                            }

                            let file_processing_end = take_timestamp();
                            let compile_command_start = file_processing_end;

                            if (result == PROCESS_RESULT_SUCCESS)
                            {
                                if (cache_missing)
                                {
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

                                    result = build_compile_commands(arena, compile_commands, compilation_units, compilation_unit_count, cwd) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;

                                    let compile_command_end = take_timestamp();

                                    if (result == PROCESS_RESULT_SUCCESS)
                                    {
                                        let compile_start = compile_command_end;
                                        result = compile(ring, compilation_units, compilation_unit_count, envp) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
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
                                                    .compilation_count = array_length(builder_indices),
                                                    .io_uring = true,
                                                },
                                                {
                                                    .artifact_path = S("build/builder"),
                                                    .compilations = disk_indices,
                                                    .compilation_count = compilation_unit_count - 1,
                                                    .run = true,
                                                },
                                            };


                                            for (u64 i = 0; i < array_length(link_units); i += 1)
                                            {
                                                LinkUnit* link_unit = &link_units[i];
                                                queue_linkage_job(arena, link_unit, compilation_units, envp);
                                            }

                                            for (u64 i = 0; i < array_length(link_units); i += 1)
                                            {
                                                process_wait_exit_queue(io_ring);
                                            }

                                            io_uring_submit_and_wait();


                                            let link_end = take_timestamp();

                                            if (verbose)
                                            {
                                                printf("Arena: %lu ns\n", ns_between(arena_init_start, arena_init_end));
                                                printf("Thread: %lu ns\n", ns_between(thread_start, thread_end));
                                                printf("Open: %lu ns\n", ns_between(open_start, open_end));
                                                printf("Stat: %lu ns\n", ns_between(stat_start, stat_end));
                                                printf("File processing: %lu ns\n", ns_between(file_processing_start, file_processing_end));
                                                printf("Compile command building: %lu ns\n", ns_between(compile_command_start, compile_command_end));
                                                printf("Compilation (%lu objects): %lu ns\n", compilation_unit_count, ns_between(compile_start, compile_end));
                                                printf("Link: %lu ns\n", ns_between(link_start, link_end));
                                            }

                                            if ((command == BUILD_COMMAND_TEST) & (result == PROCESS_RESULT_SUCCESS))
                                            {
                                                let exec_start = take_timestamp();

                                                char* args[] = {
                                                    artifact_output.pointer,
                                                    0,
                                                };
                                                let pid = spawn_process(args, envp);
                                                result = pid != -1 ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
                                                if (result == PROCESS_RESULT_SUCCESS)
                                                {
                                                    result = wait_for_process(pid);
                                                    let exec_end = take_timestamp();
                                                    if (verbose)
                                                    {
                                                        printf("Test: %lu ns\n", ns_between(exec_start, exec_end));
                                                    }
                                                    if (result == PROCESS_RESULT_SUCCESS)
                                                    {
                                                        printf("All tests succeeded!\n");
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
