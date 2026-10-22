// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The real-source Clang oracle for #125. main.c includes this after c_preprocessor_test.c, whose token/line and
// scalar/vector comparisons it shares. pp_oracle_paths collects deterministic bounded input lists without recursion;
// run_preprocessor_oracle runs one configuration, with one macro dump and explicit rejection/error accounting.
// Process and directory control stay outside the compiler's vectorized preprocessing and comparison kernels.

#include <dirent.h>
#include <fcntl.h>

#define BUSTER_PP_ORACLE_MAX_FILES 262144
#define BUSTER_PP_ORACLE_MAX_DEPTH 64
#define BUSTER_PP_ORACLE_MAX_PATH 4096
#define BUSTER_PP_ORACLE_EXTRA_ARGUMENTS 256
#define BUSTER_PP_ORACLE_EPOCH "1792454400"

typedef struct PPOraclePaths PPOraclePaths;
struct PPOraclePaths
{
    Arena arena;
    char** items;
    uint32_t count;
};

static bool pp_oracle_add_path(PPOraclePaths* paths, const char* path)
{
    const size_t length = strlen(path);
    bool result = length != 0 && length < BUSTER_PP_ORACLE_MAX_PATH && strpbrk(path, "\t\r\n") == 0 && paths->count < BUSTER_PP_ORACLE_MAX_FILES;
    char* copy = result ? arena_push(&paths->arena, length + 1, 1) : 0;
    result = result && copy != 0;
    if (result)
    {
        memcpy(copy, path, length + 1);
        paths->items[paths->count++] = copy;
    }
    if (!result)
    {
        fprintf(stderr, "oracle: input path or list exceeds its bounds: %s\n", path);
    }
    return result;
}

// Explicit file arguments can have any extension. Directory arguments select .c and .h regular files and do not
// follow directory symlinks. An inaccessible entry, excessive depth or truncated path fails the inventory.
static bool pp_oracle_paths(PPOraclePaths* paths, const char* path)
{
    struct stat info;
    bool result = stat(path, &info) == 0;
    if (result && S_ISREG(info.st_mode))
    {
        result = pp_oracle_add_path(paths, path);
    }
    else if (result && S_ISDIR(info.st_mode))
    {
        DIR* directories[BUSTER_PP_ORACLE_MAX_DEPTH];
        char (*names)[BUSTER_PP_ORACLE_MAX_PATH] = calloc(BUSTER_PP_ORACLE_MAX_DEPTH, BUSTER_PP_ORACLE_MAX_PATH);
        result = names != 0 && strlen(path) < BUSTER_PP_ORACLE_MAX_PATH;
        directories[0] = result ? opendir(path) : 0;
        result = result && directories[0] != 0;
        uint32_t depth = result ? 1 : 0;
        if (result)
        {
            strcpy(names[0], path);
        }
        while (result && depth != 0)
        {
            errno = 0;
            struct dirent* entry = readdir(directories[depth - 1]);
            if (entry == 0)
            {
                result = errno == 0;
                closedir(directories[depth - 1]);
                depth -= 1;
            }
            else if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                char child[BUSTER_PP_ORACLE_MAX_PATH];
                const int length = snprintf(child, sizeof(child), "%s/%s", names[depth - 1], entry->d_name);
                result = length > 0 && length < (int)sizeof(child) && lstat(child, &info) == 0;
                if (result && S_ISDIR(info.st_mode))
                {
                    result = depth < BUSTER_PP_ORACLE_MAX_DEPTH;
                    DIR* directory = result ? opendir(child) : 0;
                    result = result && directory != 0;
                    if (result)
                    {
                        directories[depth] = directory;
                        strcpy(names[depth], child);
                        depth += 1;
                    }
                }
                else if (result && S_ISREG(info.st_mode) && length > 2 && child[length - 2] == '.' && (child[length - 1] == 'c' || child[length - 1] == 'h'))
                {
                    result = pp_oracle_add_path(paths, child);
                }
            }
        }
        while (depth != 0)
        {
            closedir(directories[--depth]);
        }
        free(names);
    }
    else
    {
        result = false;
    }
    if (!result)
    {
        fprintf(stderr, "oracle: cannot inventory %s\n", path);
    }
    return result;
}

static bool pp_oracle_list(PPOraclePaths* paths, const char* list, const char* root)
{
    FILE* stream = fopen(list, "r");
    bool result = stream != 0;
    char* line = 0;
    size_t capacity = 0;
    ssize_t length;
    while (result && (length = getline(&line, &capacity, stream)) >= 0)
    {
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        {
            line[--length] = 0;
        }
        char path[BUSTER_PP_ORACLE_MAX_PATH];
        const int written = line[0] == '/' ? snprintf(path, sizeof(path), "%s", line) : snprintf(path, sizeof(path), "%s/%s", root, line);
        result = length > 0 && strlen(line) == (size_t)length && written > 0 && written < (int)sizeof(path) && pp_oracle_add_path(paths, path);
    }
    if (stream != 0)
    {
        result = ferror(stream) == 0 && result;
        fclose(stream);
    }
    free(line);
    if (!result)
    {
        fprintf(stderr, "oracle: cannot read the complete input list %s\n", list);
    }
    return result;
}

// Use Clang's actual search order even when a selected Clang has a different resource directory. Frozen callers
// provide -nostdinc and their explicit -isystem paths, so this probe is unnecessary for them.
static bool pp_oracle_includes(const char* executable, char** arguments, int* count, int capacity, PPOraclePaths* paths)
{
    arguments[(*count)++] = "-v";
    arguments[(*count)++] = "-x";
    arguments[(*count)++] = "c";
    PPClangOutput probe = pp_test_clang_process(executable, arguments, *count, "/dev/null", false);
    *count -= 3;
    bool result = probe.status == PP_CLANG_ACCEPTED;
    const char* start = result && probe.diagnostics != 0 ? strstr(probe.diagnostics, "#include <...> search starts here:\n") : 0;
    result = result && start != 0;
    const char* at = result ? strchr(start, '\n') + 1 : 0;
    arguments[(*count)++] = "-nostdinc";
    while (result && at[0] == ' ')
    {
        const char* end = strchr(at, '\n');
        result = end != 0 && *count + 2 < capacity;
        const size_t length = result ? (size_t)(end - at - 1) : 0;
        char* directory = result ? arena_push(&paths->arena, length + 1, 1) : 0;
        result = result && directory != 0;
        if (result)
        {
            memcpy(directory, at + 1, length);
            directory[length] = 0;
            arguments[(*count)++] = "-isystem";
            arguments[(*count)++] = directory;
            at = end + 1;
        }
    }
    result = result && strncmp(at, "End of search list.", 19) == 0;
    free(probe.text);
    free(probe.diagnostics);
    return result;
}

static void pp_oracle_reason(const char* diagnostics, char* reason, size_t capacity)
{
    const char* at = diagnostics != 0 ? diagnostics : "Clang rejected the input without diagnostics";
    const char* error = strstr(at, "error:");
    if (error != 0)
    {
        const char* line = error;
        while (line > at && line[-1] != '\n')
        {
            line -= 1;
        }
        at = line;
    }
    size_t used = 0;
    while (at[used] != 0 && at[used] != '\n' && used + 1 < capacity)
    {
        reason[used] = at[used] == '\t' || at[used] == '\r' ? ' ' : at[used];
        used += 1;
    }
    reason[used] = 0;
}

static int run_preprocessor_oracle(int argument_count, char** arguments)
{
    PPOraclePaths paths = { .arena = arena_reserve(1ull << 30) };
    paths.items = arena_push_array(&paths.arena, BUSTER_PP_ORACLE_MAX_FILES, sizeof(char*), 8);
    const int capacity = argument_count + BUSTER_PP_ORACLE_EXTRA_ARGUMENTS;
    char** clang_arguments = calloc((size_t)capacity, sizeof(char*));
    char** own_arguments = calloc((size_t)capacity, sizeof(char*));
    const char* executable = "clang";
    const char* list = 0;
    const char* root = ".";
    const char* report_path = 0;
    bool keep = false;
    bool no_standard_includes = false;
    bool ready = paths.items != 0 && clang_arguments != 0 && own_arguments != 0;
    int count = 0;
    if (ready)
    {
        clang_arguments[count++] = "--no-default-config";
        clang_arguments[count++] = "--target=x86_64-linux-gnu";
        clang_arguments[count++] = "-std=gnu17";
        clang_arguments[count++] = "-x";
        clang_arguments[count++] = "c";
    }
    for (int i = 0; ready && i < argument_count; i += 1)
    {
        const char* argument = arguments[i];
        const bool value = strcmp(argument, "--clang") == 0 || strcmp(argument, "--files-from") == 0 || strcmp(argument, "--root") == 0 || strcmp(argument, "--report") == 0;
        if (value)
        {
            ready = i + 1 < argument_count;
            if (ready)
            {
                const char* next = arguments[++i];
                if (strcmp(argument, "--clang") == 0)
                {
                    executable = next;
                }
                else if (strcmp(argument, "--files-from") == 0)
                {
                    ready = list == 0;
                    list = next;
                }
                else if (strcmp(argument, "--root") == 0)
                {
                    root = next;
                }
                else
                {
                    report_path = next;
                }
            }
        }
        else if (strcmp(argument, "--compare-clang") == 0 || strcmp(argument, "--no-default-config") == 0 || strcmp(argument, "--target=x86_64-linux-gnu") == 0)
        {
            // Fixed shared driver configuration, already supplied above.
        }
        else if (strcmp(argument, "--keep") == 0)
        {
            keep = true;
        }
        else if (argument[0] != '-')
        {
            ready = pp_oracle_paths(&paths, argument);
        }
        else
        {
            const bool takes_value = strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 || strcmp(argument, "-U") == 0 || strcmp(argument, "-isystem") == 0 ||
                                     strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 || strcmp(argument, "-include") == 0 || strcmp(argument, "-x") == 0;
            no_standard_includes |= strcmp(argument, "-nostdinc") == 0;
            clang_arguments[count++] = arguments[i];
            if (takes_value)
            {
                ready = i + 1 < argument_count;
                if (ready)
                {
                    clang_arguments[count++] = arguments[++i];
                }
            }
        }
    }
    ready = ready && (list == 0 || pp_oracle_list(&paths, list, root)) && paths.count != 0;
    if (ready)
    {
        qsort(paths.items, paths.count, sizeof(char*), pp_test_compare_lines);
        uint32_t unique = 0;
        for (uint32_t i = 0; i < paths.count; i += 1)
        {
            if (unique == 0 || strcmp(paths.items[i], paths.items[unique - 1]) != 0)
            {
                paths.items[unique++] = paths.items[i];
            }
        }
        paths.count = unique;
    }
    const bool fixed_epoch = getenv("SOURCE_DATE_EPOCH") == 0;
    ready = ready && (!fixed_epoch || setenv("SOURCE_DATE_EPOCH", BUSTER_PP_ORACLE_EPOCH, 1) == 0);
    ready = ready && (no_standard_includes || pp_oracle_includes(executable, clang_arguments, &count, capacity, &paths));
    int own_count = 0;
    for (int i = 0; ready && i < count; i += 1)
    {
        const char* argument = clang_arguments[i];
        if (strcmp(argument, "--no-default-config") != 0 && strcmp(argument, "--target=x86_64-linux-gnu") != 0 && strcmp(argument, "-pipe") != 0 && strcmp(argument, "-H") != 0)
        {
            own_arguments[own_count++] = clang_arguments[i];
        }
    }
    char directory[BUSTER_PP_ORACLE_MAX_PATH] = { 0 };
    const char* temporary = getenv("TMPDIR");
    const int written = snprintf(directory, sizeof(directory), "%s/buster-pp-oracle-XXXXXX", temporary != 0 ? temporary : "/tmp");
    const bool made_directory = ready && written > 0 && written < (int)sizeof(directory) && mkdtemp(directory) != 0;
    ready = ready && made_directory;
    PPOptions options;
    const char* input;
    const char* output;
    bool markers;
    if (ready)
    {
        own_arguments[own_count] = paths.items[0];
        ready = pp_parse_options(own_count + 1, own_arguments, &options, &paths.arena, &input, &output, &markers) && output == 0;
    }
    char predefines[BUSTER_PP_ORACLE_MAX_PATH + 32];
    snprintf(predefines, sizeof(predefines), "%s/predefines.h", directory);
    if (ready)
    {
        PPClangOutput macros = pp_test_clang_process(executable, clang_arguments, count, "/dev/null", true);
        ready = macros.status == PP_CLANG_ACCEPTED;
        if (macros.diagnostics != 0)
        {
            fputs(macros.diagnostics, stderr);
        }
        FILE* stream = ready ? fopen(predefines, "w") : 0;
        ready = stream != 0 && fwrite(macros.text, 1, macros.length, stream) == macros.length;
        if (stream != 0)
        {
            ready = fclose(stream) == 0 && ready;
        }
        free(macros.text);
        free(macros.diagnostics);
        options.predefines_path = predefines;
    }
    FILE* report = ready && report_path != 0 ? fopen(report_path, "wx") : 0;
    ready = ready && (report_path == 0 || report != 0);
    if (report != 0)
    {
        fputs("status\tpath\ttokens\treason\n", report);
    }
    uint32_t identical = 0;
    uint32_t different = 0;
    uint32_t skipped = 0;
    uint32_t errors = !ready;
    uint64_t tokens = 0;
    for (uint32_t i = 0; ready && i < paths.count; i += 1)
    {
        const char* path = paths.items[i];
        const int fd = open(path, O_RDONLY | O_CLOEXEC);
        struct stat info;
        const bool readable = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
        if (fd >= 0)
        {
            close(fd);
        }
        PPClangOutput clang = readable ? pp_test_clang_process(executable, clang_arguments, count, path, false) : (PPClangOutput){ 0 };
        char reason[2048] = { 0 };
        const char* status;
        uint64_t file_tokens = 0;
        if (clang.status == PP_CLANG_ACCEPTED)
        {
            const bool same = pp_test_file_output(&options, path, true, clang.text, clang.length, &file_tokens, 0);
            identical += same;
            different += !same;
            status = same ? "identical" : "different";
        }
        else if (clang.status == PP_CLANG_REJECTED)
        {
            skipped += 1;
            status = "skipped";
            pp_oracle_reason(clang.diagnostics, reason, sizeof(reason));
            printf("skipped: %s: %s\n", path, reason);
        }
        else
        {
            errors += 1;
            status = "error";
            snprintf(reason, sizeof(reason), "%s", readable ? "Clang process or output failure" : "input is not a readable regular file");
            fprintf(stderr, "oracle: %s: %s\n", path, reason);
        }
        if (clang.diagnostics != 0)
        {
            fputs(clang.diagnostics, stderr);
        }
        if (report != 0)
        {
            fprintf(report, "%s\t%s\t%lu\t%s\n", status, path, (unsigned long)file_tokens, reason);
        }
        tokens += file_tokens;
        free(clang.text);
        free(clang.diagnostics);
    }
    if (report != 0)
    {
        errors += fclose(report) != 0;
    }
    printf("files: compared=%u identical=%u different=%u skipped=%u errors=%u (%lu tokens)\n", identical + different, identical, different, skipped, errors, (unsigned long)tokens);
    if (!ready)
    {
        fprintf(stderr, "usage: compiler test-preprocessor --compare-clang [--clang PATH] [--files-from LIST --root DIR] [--report TSV] [--keep] [options] corpus...\n");
    }
    if (keep && made_directory)
    {
        printf("oracle temporary directory: %s\n", directory);
    }
    else if (made_directory)
    {
        unlink(predefines);
        rmdir(directory);
    }
    if (fixed_epoch)
    {
        unsetenv("SOURCE_DATE_EPOCH");
    }
    free(clang_arguments);
    free(own_arguments);
    arena_release(&paths.arena);
    return errors == 0 && different == 0 && identical != 0 ? 0 : 1;
}

static int run_preprocessor_tests(int argument_count, char** arguments)
{
    bool oracle = false;
    for (int i = 0; i < argument_count; i += 1)
    {
        oracle |= strcmp(arguments[i], "--compare-clang") == 0;
    }
    // --clang alone retains the fixture interface; --compare-clang accepts --clang PATH.
    return oracle ? run_preprocessor_oracle(argument_count, arguments) : run_preprocessor_fixture_tests(argument_count, arguments);
}
