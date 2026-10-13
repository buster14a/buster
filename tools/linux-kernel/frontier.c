// #3186 Linux x86-64 frontier; opt-in cloud diagnostic.
// main owns run/record-cc/record-host/buster-cc. kernel_run bounds stage
// processes, kernel_record preserves argv/cwd/status, kernel_replay consumes
// target-C actions without dropping flags. kernel_source_verify pins source
// bytes independently of the Git index; kernel_source_controls proves its
// refusals. No kernel or compiler source edits.
#include <buster/lib/base.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>

#define KERNEL_PIN "adc218676eef25575469234709c2d87185ca223a"
#define KERNEL_PATH_BYTES 4096
#define KERNEL_RECORD_BYTES (256 * 1024)
#define KERNEL_MAX_ARGUMENTS 1024
#define KERNEL_REPLAY_LIMIT 24

typedef struct KernelContext
{
    char source[KERNEL_PATH_BYTES];
    char compiler[KERNEL_PATH_BYTES];
    char output[KERNEL_PATH_BYTES];
    char self[KERNEL_PATH_BYTES];
    char reference[KERNEL_PATH_BYTES];
    char candidate[KERNEL_PATH_BYTES];
    char records[KERNEL_PATH_BYTES];
    char initramfs[KERNEL_PATH_BYTES];
    char config[KERNEL_PATH_BYTES];
} KernelContext;

extern char** environ;

BUSTER_GLOBAL_LOCAL KernelContext kernel_context;
BUSTER_GLOBAL_LOCAL char kernel_record_bytes[KERNEL_RECORD_BYTES];
BUSTER_GLOBAL_LOCAL char* kernel_arguments[KERNEL_MAX_ARGUMENTS];

BUSTER_GLOBAL_LOCAL bool kernel_path(char* out, char const* root, char const* leaf)
{
    int bytes = snprintf(out, KERNEL_PATH_BYTES, "%s/%s", root, leaf);
    bool valid = bytes > 0 && bytes < KERNEL_PATH_BYTES;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_write(char const* path, char const* bytes, size_t size)
{
    FILE* file = fopen(path, "wb");
    bool valid = file != 0;
    if (file)
    {
        valid = fwrite(bytes, 1, size, file) == size;
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_contains(char const* path, char const* needle)
{
    FILE* file = fopen(path, "rb");
    bool found = false;
    if (file)
    {
        size_t count = fread(kernel_record_bytes, 1, sizeof(kernel_record_bytes) - 1, file);
        kernel_record_bytes[count] = 0;
        found = strstr(kernel_record_bytes, needle) != 0 && !ferror(file);
        found = fclose(file) == 0 && found;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL double kernel_seconds(void)
{
    struct timespec value;
    int status = clock_gettime(CLOCK_MONOTONIC, &value);
    double seconds = status == 0 ? (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0 : -1;
    return seconds;
}

// NUL fields: cwd, then argv (including whitespace/empty arguments).
BUSTER_GLOBAL_LOCAL bool kernel_argv(char const* path, char const* cwd, char* const* arguments)
{
    FILE* file = fopen(path, "wb");
    bool valid = file != 0;
    if (file)
    {
        valid = fwrite(cwd, 1, strlen(cwd) + 1, file) == strlen(cwd) + 1;
        for (size_t i = 0; arguments[i] && valid; ++i)
        {
            size_t size = strlen(arguments[i]) + 1;
            valid = fwrite(arguments[i], 1, size, file) == size;
        }
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

// input, when set, replaces the inherited stdin of the stage process.
BUSTER_GLOBAL_LOCAL int kernel_run_input(char const* name, char const* cwd, char* const* arguments, unsigned budget, char const* input)
{
    char log[KERNEL_PATH_BYTES], argv_path[KERNEL_PATH_BYTES], result_path[KERNEL_PATH_BYTES];
    char leaf[256];
    int result = 125;
    bool valid = snprintf(leaf, sizeof(leaf), "%s.log", name) < (int)sizeof(leaf) &&
                 kernel_path(log, kernel_context.output, leaf);
    valid = snprintf(leaf, sizeof(leaf), "%s.argv", name) < (int)sizeof(leaf) &&
            kernel_path(argv_path, kernel_context.output, leaf) && valid;
    valid = snprintf(leaf, sizeof(leaf), "%s.result", name) < (int)sizeof(leaf) &&
            kernel_path(result_path, kernel_context.output, leaf) && valid;
    valid = valid && kernel_argv(argv_path, cwd, arguments);
    int fd = valid ? open(log, O_CREAT | O_EXCL | O_WRONLY, 0600) : -1;
    double start = kernel_seconds();
    if (fd >= 0 && start >= 0)
    {
        pid_t child = fork();
        if (child == 0)
        {
            int in = input ? open(input, O_RDONLY) : STDIN_FILENO;
            if (setpgid(0, 0) != 0 || chdir(cwd) != 0 || in < 0 || dup2(in, STDIN_FILENO) < 0 ||
                dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
            {
                _exit(125);
            }
            close(fd);
            if (in != STDIN_FILENO)
            {
                close(in);
            }
            execvp(arguments[0], arguments);
            perror(arguments[0]);
            _exit(127);
        }
        else if (child > 0)
        {
            int status = 0;
            struct rusage usage = {0};
            bool done = false, timed_out = false;
            while (!done)
            {
                pid_t waited = wait4(child, &status, WNOHANG, &usage);
                if (waited == child)
                {
                    done = true;
                }
                else if (waited < 0 && errno != EINTR)
                {
                    done = true;
                    status = 125 << 8;
                }
                else
                {
                    double now = kernel_seconds();
                    if (now < 0 || now - start >= (double)budget)
                    {
                        kill(-child, SIGKILL);
                        kill(child, SIGKILL);
                        pid_t reaped;
                        do
                        {
                            reaped = wait4(child, &status, 0, &usage);
                        } while (reaped < 0 && errno == EINTR);
                        timed_out = true;
                        done = true;
                    }
                    else
                    {
                        struct timespec pause = {.tv_nsec = 10000000};
                        nanosleep(&pause, 0);
                    }
                }
            }
            result = timed_out ? 124 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            char facts[512];
            int bytes = snprintf(facts, sizeof(facts),
                                 "stage=%s\nexit=%d\ntimeout=%u\nwall_seconds=%.6f\n"
                                 "wait4_maxrss_kib=%ld\nwait4_user_seconds=%.6f\nwait4_system_seconds=%.6f\n",
                                 name, result, timed_out, kernel_seconds() - start, usage.ru_maxrss,
                                 (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / 1000000.0,
                                 (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec / 1000000.0);
            if (bytes < 0 || bytes >= (int)sizeof(facts) || !kernel_write(result_path, facts, (size_t)bytes))
            {
                result = 125;
            }
        }
    }
    if (fd >= 0)
    {
        close(fd);
    }
    printf("LINUX_STAGE name=%s exit=%d\n", name, result);
    fflush(stdout);
    return result;
}

BUSTER_GLOBAL_LOCAL int kernel_run(char const* name, char const* cwd, char* const* arguments, unsigned budget)
{
    int result = kernel_run_input(name, cwd, arguments, budget, 0);
    return result;
}

// Reserves a fresh record name with O_EXCL. Process IDs wrap during a long
// Kbuild run, so a PID-only name would let a later compiler process silently
// overwrite an earlier action's record and undercount the census.
BUSTER_GLOBAL_LOCAL bool kernel_reserve(char const* directory, char const* kind, char* record, char* result_path)
{
    bool reserved = false, valid = true;
    for (unsigned attempt = 0; valid && !reserved && attempt < 4096; ++attempt)
    {
        char leaf[128];
        int n = snprintf(leaf, sizeof(leaf), "%s-%ld-%u.argv", kind, (long)getpid(), attempt);
        valid = n > 0 && n < (int)sizeof(leaf) && kernel_path(record, directory, leaf);
        int fd = valid ? open(record, O_CREAT | O_EXCL | O_WRONLY, 0600) : -1;
        valid = valid && (fd >= 0 || errno == EEXIST);
        reserved = fd >= 0;
        if (reserved)
        {
            valid = close(fd) == 0;
            n = snprintf(leaf, sizeof(leaf), "%s-%ld-%u.result", kind, (long)getpid(), attempt);
            valid = valid && n > 0 && n < (int)sizeof(leaf) && kernel_path(result_path, directory, leaf);
        }
    }
    valid = valid && reserved;
    return valid;
}

BUSTER_GLOBAL_LOCAL int kernel_record(int argc, char** argv, bool host, bool buster)
{
    char cwd[KERNEL_PATH_BYTES], record[KERNEL_PATH_BYTES], result_path[KERNEL_PATH_BYTES];
    char const* directory = getenv("BUSTER_KERNEL_RECORDS");
    char const* compiler = getenv("BUSTER_KERNEL_COMPILER");
    char const* reference_cc = getenv("BUSTER_KERNEL_REFERENCE_CC");
    int result = 125;
    bool valid = directory && (!buster || compiler) && getcwd(cwd, sizeof(cwd)) &&
                 argc < KERNEL_MAX_ARGUMENTS - 3;
    if (valid)
    {
        valid = kernel_reserve(directory, buster ? "buster" : host ? "host" : "target", record, result_path);
        size_t count = 0;
        kernel_arguments[count++] = (char*)(buster ? compiler : reference_cc ? reference_cc : "gcc-14");
        if (buster)
        {
            kernel_arguments[count++] = "cc";
        }
        for (int i = 2; i < argc; ++i)
        {
            kernel_arguments[count++] = argv[i];
        }
        kernel_arguments[count] = 0;
        valid = valid && kernel_argv(record, cwd, kernel_arguments);
        if (valid)
        {
            pid_t child = fork();
            if (child == 0)
            {
                execvp(kernel_arguments[0], kernel_arguments);
                perror(kernel_arguments[0]);
                _exit(127);
            }
            else if (child > 0)
            {
                int status = 0;
                pid_t waited;
                do
                {
                    waited = waitpid(child, &status, 0);
                } while (waited < 0 && errno == EINTR);
                result = waited == child ? (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)) : 125;
                char facts[128];
                int bytes = snprintf(facts, sizeof(facts), "exit=%d\nrole=%s\n", result, buster ? "buster-target" : host ? "trusted-host" : "trusted-target");
                if (bytes < 0 || bytes >= (int)sizeof(facts) || !kernel_write(result_path, facts, (size_t)bytes))
                {
                    result = 125;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool kernel_read_record(char const* path, size_t* count)
{
    FILE* file = fopen(path, "rb");
    bool valid = file != 0;
    *count = 0;
    if (file)
    {
        size_t size = fread(kernel_record_bytes, 1, sizeof(kernel_record_bytes), file);
        valid = size > 0 && size < sizeof(kernel_record_bytes) && !ferror(file) &&
                kernel_record_bytes[size - 1] == 0;
        if (valid)
        {
            size_t offset = 0;
            while (offset < size && *count < KERNEL_MAX_ARGUMENTS - 4)
            {
                kernel_arguments[(*count)++] = kernel_record_bytes + offset;
                offset += strlen(kernel_record_bytes + offset) + 1;
            }
            valid = offset == size && *count >= 2;
        }
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_target_c(size_t count)
{
    bool compile = false, source = false, output = false;
    for (size_t i = 2; i < count; ++i)
    {
        char const* argument = kernel_arguments[i];
        size_t size = strlen(argument);
        compile = compile || strcmp(argument, "-c") == 0 || strcmp(argument, "-S") == 0;
        source = source || (size > 2 && strcmp(argument + size - 2, ".c") == 0 &&
                            strstr(argument, ".tmp") == 0);
        if (strcmp(argument, "-o") == 0 && i + 1 < count)
        {
            char const* destination = kernel_arguments[i + 1];
            size_t bytes = strlen(destination);
            output = bytes > 2 && destination[bytes - 2] == '.' &&
                     (destination[bytes - 1] == 'o' || destination[bytes - 1] == 's') &&
                     strstr(destination, ".tmp") == 0;
        }
    }
    bool valid = compile && source && output;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_index_row(FILE* index, char const* record, char const* output, bool attempted, int status)
{
    int bytes = fprintf(index, "%s\t%s\t%s\t%d\n", record, output, attempted ? "attempted" : "unattempted", status);
    bool valid = bytes > 0;
    return valid;
}

typedef struct KernelSelection
{
    char record[128];
    char output[KERNEL_PATH_BYTES];
} KernelSelection;

BUSTER_GLOBAL_LOCAL KernelSelection kernel_selection[KERNEL_REPLAY_LIMIT];

BUSTER_GLOBAL_LOCAL int kernel_replay(void)
{
    char index_path[KERNEL_PATH_BYTES];
    bool valid = kernel_path(index_path, kernel_context.output, "target-c.tsv");
    FILE* index = valid ? fopen(index_path, "wb") : 0;
    DIR* directory = index ? opendir(kernel_context.records) : 0;
    unsigned required = 0, selected = 0, attempted = 0, passed = 0, malformed = 0;
    if (index)
    {
        fprintf(index, "record\toutput\tattempted\texit\n");
    }
    if (directory)
    {
        struct dirent* entry;
        while ((entry = readdir(directory)) != 0)
        {
            size_t length = strlen(entry->d_name);
            if (strncmp(entry->d_name, "target-", 7) == 0 && length > 5 &&
                strcmp(entry->d_name + length - 5, ".argv") == 0)
            {
                char path[KERNEL_PATH_BYTES];
                size_t count = 0;
                if (!kernel_path(path, kernel_context.records, entry->d_name) || !kernel_read_record(path, &count))
                {
                    ++malformed;
                }
                else if (kernel_target_c(count))
                {
                    ++required;
                    char const* original_output = "";
                    for (size_t i = 2; i + 1 < count; ++i)
                    {
                        if (strcmp(kernel_arguments[i], "-o") == 0)
                        {
                            original_output = kernel_arguments[i + 1];
                        }
                    }
                    // Stable selection by original object path, independent
                    // of filesystem enumeration and compiler process IDs.
                    unsigned slot = 0;
                    while (slot < selected && strcmp(kernel_selection[slot].output, original_output) <= 0)
                    {
                        ++slot;
                    }
                    if (slot < KERNEL_REPLAY_LIMIT && strlen(entry->d_name) < sizeof(kernel_selection[slot].record) &&
                        strlen(original_output) < sizeof(kernel_selection[slot].output))
                    {
                        unsigned end = selected < KERNEL_REPLAY_LIMIT ? selected++ : KERNEL_REPLAY_LIMIT - 1;
                        while (end > slot)
                        {
                            kernel_selection[end] = kernel_selection[end - 1];
                            --end;
                        }
                        strcpy(kernel_selection[slot].record, entry->d_name);
                        strcpy(kernel_selection[slot].output, original_output);
                    }
                    valid = kernel_index_row(index, entry->d_name, original_output, false, -1) && valid;
                }
            }
        }
        valid = closedir(directory) == 0 && valid;
    }
    else
    {
        valid = false;
    }
    for (unsigned row = 0; row < selected && valid; ++row)
    {
        char path[KERNEL_PATH_BYTES];
        size_t count = 0;
        valid = kernel_path(path, kernel_context.records, kernel_selection[row].record) &&
                kernel_read_record(path, &count);
        if (valid)
        {
            char name[64], destination[KERNEL_PATH_BYTES], dependency[KERNEL_PATH_BYTES + 32], leaf[64];
            snprintf(name, sizeof(name), "replay-%u", row);
            size_t size = strlen(kernel_selection[row].output);
            snprintf(leaf, sizeof(leaf), "replay-%u.%c", row, kernel_selection[row].output[size - 1]);
            valid = kernel_path(destination, kernel_context.output, leaf);
            int bytes = snprintf(dependency, sizeof(dependency), "-Wp,-MMD,%s/replay-%u.d", kernel_context.output, row);
            valid = bytes > 0 && bytes < (int)sizeof(dependency) && valid;
            if (valid)
            {
                char* arguments[KERNEL_MAX_ARGUMENTS] = {0};
                size_t n = 0;
                arguments[n++] = kernel_context.compiler;
                arguments[n++] = "cc";
                for (size_t i = 2; i < count; ++i)
                {
                    if (i > 2 && strcmp(kernel_arguments[i - 1], "-o") == 0)
                    {
                        arguments[n++] = destination;
                    }
                    else if (strncmp(kernel_arguments[i], "-Wp,-MMD,", 9) == 0)
                    {
                        arguments[n++] = dependency;
                    }
                    else
                    {
                        arguments[n++] = kernel_arguments[i];
                    }
                }
                int status = kernel_run(name, kernel_arguments[0], arguments, 15);
                ++attempted;
                passed += status == 0 ? 1u : 0u;
                valid = kernel_index_row(index, kernel_selection[row].record, kernel_selection[row].output, true, status) && valid;
            }
        }
    }
    if (index)
    {
        valid = fclose(index) == 0 && valid;
    }
    char summary[512], path[KERNEL_PATH_BYTES];
    int bytes = snprintf(summary, sizeof(summary),
                         "reference_target_c_actions=%u\nreplay_attempted=%u\nreplay_passed=%u\n"
                         "replay_failed=%u\nreplay_unattempted=%u\nmalformed_records=%u\n"
                         "candidate_generated_inputs=unqualified\nqualified_kernel=no\nperformance=unvalidated\n",
                         required, attempted, passed, attempted - passed, required - attempted, malformed);
    valid = bytes > 0 && bytes < (int)sizeof(summary) && kernel_path(path, kernel_context.output, "frontier.txt") &&
            kernel_write(path, summary, (size_t)bytes) && valid;
    printf("%s", summary);
    int result = valid && malformed == 0 && required > 0 && attempted > 0 ? 0 : 1;
    return result;
}

// Clears every inherited GIT_* override (repository, index, object, config,
// replacement and namespace redirections), then disables replacement objects
// and system/global configuration for all later Git and Kbuild children.
BUSTER_GLOBAL_LOCAL bool kernel_git_environment(void)
{
    bool valid = true, removed = true;
    while (removed && valid)
    {
        removed = false;
        for (char** entry = environ; *entry && !removed; ++entry)
        {
            if (strncmp(*entry, "GIT_", 4) == 0)
            {
                char name[256];
                size_t length = strcspn(*entry, "=");
                valid = length < sizeof(name);
                if (valid)
                {
                    memcpy(name, *entry, length);
                    name[length] = 0;
                    valid = unsetenv(name) == 0;
                    removed = true;
                }
            }
        }
    }
    valid = valid && setenv("GIT_NO_REPLACE_OBJECTS", "1", 1) == 0 && setenv("GIT_CONFIG_NOSYSTEM", "1", 1) == 0 &&
            setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1) == 0 && setenv("GIT_ATTR_NOSYSTEM", "1", 1) == 0;
    return valid;
}

// Loads a whole file; the caller frees the NUL-terminated result.
BUSTER_GLOBAL_LOCAL char* kernel_load(char const* path, size_t* size)
{
    FILE* file = fopen(path, "rb");
    char* bytes = 0;
    struct stat facts;
    *size = 0;
    if (file && fstat(fileno(file), &facts) == 0 && facts.st_size >= 0)
    {
        bytes = malloc((size_t)facts.st_size + 1);
        size_t count = bytes ? fread(bytes, 1, (size_t)facts.st_size, file) : 0;
        if (bytes && (count != (size_t)facts.st_size || ferror(file)))
        {
            free(bytes);
            bytes = 0;
        }
        else if (bytes)
        {
            bytes[count] = 0;
            *size = count;
        }
    }
    if (file)
    {
        fclose(file);
    }
    return bytes;
}

BUSTER_GLOBAL_LOCAL char* kernel_load_log(char const* stage, size_t* size)
{
    char leaf[256], path[KERNEL_PATH_BYTES];
    bool valid = snprintf(leaf, sizeof(leaf), "%s.log", stage) < (int)sizeof(leaf) &&
                 kernel_path(path, kernel_context.output, leaf);
    *size = 0;
    char* bytes = valid ? kernel_load(path, size) : 0;
    return bytes;
}

// The stage log must be exactly one line holding expected.
BUSTER_GLOBAL_LOCAL bool kernel_log_equals(char const* stage, char const* expected)
{
    size_t size = 0;
    char* bytes = kernel_load_log(stage, &size);
    bool valid = bytes && size == strlen(expected) + 1 && memcmp(bytes, expected, size - 1) == 0 && bytes[size - 1] == '\n';
    free(bytes);
    return valid;
}

// Counts every non-directory entry below source except the top-level .git,
// with an explicit stack. Entries that are not regular files or symlinks
// count twice so they can never balance the tracked-entry count.
BUSTER_GLOBAL_LOCAL bool kernel_count_files(char const* source, size_t* files)
{
    size_t capacity = 64, depth = 0;
    char** stack = malloc(capacity * sizeof(char*));
    bool valid = stack != 0;
    *files = 0;
    if (valid)
    {
        stack[depth++] = strdup(source);
        valid = stack[0] != 0;
    }
    while (valid && depth > 0)
    {
        char* directory_path = stack[--depth];
        DIR* directory = opendir(directory_path);
        valid = directory != 0;
        struct dirent* entry = 0;
        while (valid && (entry = readdir(directory)) != 0)
        {
            char path[KERNEL_PATH_BYTES];
            struct stat facts;
            bool skip = strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
                        (strcmp(entry->d_name, ".git") == 0 && strcmp(directory_path, source) == 0);
            valid = skip || (kernel_path(path, directory_path, entry->d_name) && lstat(path, &facts) == 0);
            if (valid && !skip && S_ISDIR(facts.st_mode))
            {
                if (depth == capacity)
                {
                    char** grown = realloc(stack, capacity * 2 * sizeof(char*));
                    valid = grown != 0;
                    stack = grown ? grown : stack;
                    capacity = grown ? capacity * 2 : capacity;
                }
                stack[depth] = valid ? strdup(path) : 0;
                valid = valid && stack[depth++] != 0;
            }
            else if (valid && !skip)
            {
                *files += S_ISREG(facts.st_mode) || S_ISLNK(facts.st_mode) ? 1u : 2u;
            }
        }
        if (directory)
        {
            valid = closedir(directory) == 0 && valid;
        }
        free(directory_path);
    }
    while (depth > 0)
    {
        free(stack[--depth]);
    }
    free(stack);
    return valid;
}

// Verifies source byte-for-byte against pin's tree without consulting the
// index, so assume-unchanged/skip-worktree flags, sparse checkouts, filters,
// replacement refs and GIT_* overrides cannot hide drift. Every tracked path
// must exist with its exact type, executable bit and raw blob identity, and no
// other file, symlink or special entry (ignored or untracked) may exist
// outside the top-level .git. The local object database is trusted as
// content-addressed; it is not re-hashed.
BUSTER_GLOBAL_LOCAL bool kernel_source_verify(char const* source, char const* pin, char const* stage)
{
    char name[4][128], list[KERNEL_PATH_BYTES], links[KERNEL_PATH_BYTES], path[KERNEL_PATH_BYTES], leaf[192];
    char const* suffix[4] = {"revision", "toplevel", "tree", "hash"};
    bool valid = kernel_git_environment();
    for (int i = 0; i < 4 && valid; ++i)
    {
        valid = snprintf(name[i], sizeof(name[i]), "%s-%s", stage, suffix[i]) < (int)sizeof(name[i]);
    }
    valid = valid && snprintf(leaf, sizeof(leaf), "%s-paths.txt", stage) < (int)sizeof(leaf) &&
            kernel_path(list, kernel_context.output, leaf) &&
            snprintf(leaf, sizeof(leaf), "%s-links", stage) < (int)sizeof(leaf) &&
            kernel_path(links, kernel_context.output, leaf) && mkdir(links, 0700) == 0;
    char* revision[] = {"git", "--no-replace-objects", "rev-parse", "--verify", "HEAD^{commit}", 0};
    char* toplevel[] = {"git", "--no-replace-objects", "rev-parse", "--show-toplevel", 0};
    char* tree[] = {"git", "--no-replace-objects", "ls-tree", "-r", "-z", "--full-tree", (char*)pin, 0};
    char* hash[] = {"git", "--no-replace-objects", "hash-object", "--no-filters", "--stdin-paths", 0};
    valid = valid && kernel_run(name[0], source, revision, 30) == 0 && kernel_log_equals(name[0], pin) &&
            kernel_run(name[1], source, toplevel, 30) == 0 && kernel_log_equals(name[1], source) &&
            kernel_run(name[2], source, tree, 60) == 0;
    size_t tree_size = 0, entries = 0, verified = 0, files = 0;
    char* tree_bytes = valid ? kernel_load_log(name[2], &tree_size) : 0;
    valid = tree_bytes != 0 && tree_size > 0 && tree_bytes[tree_size - 1] == 0;
    for (size_t i = 0; valid && i < tree_size; ++i)
    {
        entries += tree_bytes[i] == 0 ? 1u : 0u;
    }
    char** oids = valid ? calloc(entries, sizeof(char*)) : 0;
    FILE* paths = oids ? fopen(list, "wb") : 0;
    valid = paths != 0;
    for (size_t offset = 0, row = 0; valid && offset < tree_size; ++row)
    {
        // "<mode> blob <oid>\t<path>\0": ls-tree -r lists only leaf entries.
        char* entry = tree_bytes + offset;
        char* type = strchr(entry, ' ');
        char* oid = type ? strchr(type + 1, ' ') : 0;
        char* tab = oid ? strchr(oid + 1, '\t') : 0;
        offset += strlen(entry) + 1;
        valid = tab && strncmp(type, " blob ", 6) == 0 && !strchr(tab + 1, '\n') && kernel_path(path, source, tab + 1);
        struct stat facts;
        bool link = valid && strncmp(entry, "120000 ", 7) == 0;
        bool executable = valid && strncmp(entry, "100755 ", 7) == 0;
        valid = valid && (link || executable || strncmp(entry, "100644 ", 7) == 0) && lstat(path, &facts) == 0;
        if (valid && link)
        {
            // Hash the symlink target bytes, never the file it points to.
            char target[KERNEL_PATH_BYTES], copy[KERNEL_PATH_BYTES];
            ssize_t bytes = S_ISLNK(facts.st_mode) ? readlink(path, target, sizeof(target)) : -1;
            valid = bytes >= 0 && bytes < (ssize_t)sizeof(target) &&
                    snprintf(leaf, sizeof(leaf), "%zu", row) < (int)sizeof(leaf) && kernel_path(copy, links, leaf) &&
                    kernel_write(copy, target, (size_t)bytes) && fprintf(paths, "%s\n", copy) > 0;
        }
        else if (valid)
        {
            valid = S_ISREG(facts.st_mode) && ((facts.st_mode & S_IXUSR) != 0) == executable &&
                    fprintf(paths, "%s\n", path) > 0;
        }
        if (valid)
        {
            *tab = 0;
            oids[row] = oid + 1;
        }
    }
    if (paths)
    {
        valid = fclose(paths) == 0 && valid;
    }
    valid = valid && kernel_run_input(name[3], source, hash, 300, list) == 0;
    size_t hash_size = 0;
    char* hash_bytes = valid ? kernel_load_log(name[3], &hash_size) : 0;
    bool matching = hash_bytes != 0;
    for (char* line = hash_bytes; matching && *line && verified < entries;)
    {
        char* end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : 0;
        matching = end && strlen(oids[verified]) == length && strncmp(line, oids[verified], length) == 0;
        verified += matching ? 1u : 0u;
        line = matching ? end + 1 : line;
    }
    valid = valid && verified == entries && kernel_count_files(source, &files) && files == entries;
    printf("LINUX_SOURCE_VERIFY stage=%s entries=%zu verified=%zu files=%zu result=%s\n",
           stage, entries, verified, files, valid ? "pass" : "fail");
    fflush(stdout);
    free(hash_bytes);
    free(oids);
    free(tree_bytes);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_config_matches(void)
{
    char path[KERNEL_PATH_BYTES];
    bool valid = kernel_path(path, kernel_context.reference, ".config");
    char const* required[] = {"CONFIG_64BIT=y\n", "CONFIG_X86_64=y\n", "CONFIG_BINFMT_ELF=y\n",
        "CONFIG_BLK_DEV_INITRD=y\n", "CONFIG_SERIAL_8250_CONSOLE=y\n", "CONFIG_TMPFS=y\n",
        "CONFIG_FUTEX=y\n", "CONFIG_POSIX_TIMERS=y\n", "CONFIG_INET=y\n",
        "CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE=y\n", "CONFIG_RELOCATABLE=y\n", "CONFIG_PCI=y\n", "CONFIG_ACPI=y\n"};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]) && valid; ++i)
    {
        valid = kernel_contains(path, required[i]);
    }
    valid = valid && !kernel_contains(path, "CONFIG_RUST=y\n") && !kernel_contains(path, "CONFIG_MODULES=y\n") && !kernel_contains(path, "CONFIG_RANDOMIZE_BASE=y\n");
    return valid;
}

BUSTER_GLOBAL_LOCAL int kernel_frontier(char const* fragment)
{
    KernelContext* context = &kernel_context;
    bool valid = setenv("BUSTER_KERNEL_RECORDS", context->records, 1) == 0 &&
                 setenv("BUSTER_KERNEL_COMPILER", context->compiler, 1) == 0 &&
                 setenv("KBUILD_BUILD_TIMESTAMP", "2024-11-17 22:15:08 +0000", 1) == 0 &&
                 setenv("KBUILD_BUILD_USER", "buster-kernel-control", 1) == 0 &&
                 setenv("KBUILD_BUILD_HOST", "cloud", 1) == 0 &&
                 setenv("KBUILD_BUILD_VERSION", "1", 1) == 0 &&
                 setenv("LC_ALL", "C", 1) == 0 && setenv("TZ", "UTC", 1) == 0;
    char cc[KERNEL_PATH_BYTES + 64], host[KERNEL_PATH_BYTES + 64], buster[KERNEL_PATH_BYTES + 64];
    char reference[KERNEL_PATH_BYTES + 8], candidate[KERNEL_PATH_BYTES + 8], config[KERNEL_PATH_BYTES + 32];
    snprintf(cc, sizeof(cc), "CC=%s record-cc", context->self);
    snprintf(host, sizeof(host), "HOSTCC=%s record-host", context->self);
    snprintf(buster, sizeof(buster), "CC=%s buster-cc", context->self);
    snprintf(reference, sizeof(reference), "O=%s", context->reference);
    snprintf(candidate, sizeof(candidate), "O=%s", context->candidate);
    snprintf(config, sizeof(config), "KCONFIG_ALLCONFIG=%s", fragment);
    // Before any compiler runs, the source must be the pin's exact bytes.
    valid = valid && kernel_source_verify(context->source, KERNEL_PIN, "source-before");
    char log[KERNEL_PATH_BYTES];
    char* configure[] = {"make", "-C", context->source, reference, "ARCH=x86", cc, host, config, "V=1", "allnoconfig", 0};
    valid = valid && kernel_run("reference-configure", context->source, configure, 120) == 0 && kernel_config_matches();
    char* build[] = {"make", "-C", context->source, reference, "ARCH=x86", cc, host, "V=1", "-j2", "bzImage", 0};
    valid = valid && kernel_run("reference-build", context->source, build, 600) == 0;
    char kernel[KERNEL_PATH_BYTES];
    valid = kernel_path(kernel, context->reference, "arch/x86/boot/bzImage") && valid;
    char* boot[] = {"qemu-system-x86_64", "-machine", "pc,accel=tcg", "-cpu", "max", "-smp", "1",
        "-m", "128", "-nodefaults", "-display", "none", "-serial", "stdio", "-monitor", "none", "-nic", "none",
        "-no-reboot", "-d", "guest_errors,cpu_reset", "-kernel", kernel, "-initrd", context->initramfs,
        "-append", "console=ttyS0 earlycon=uart8250,io,0x3f8,115200 rdinit=/init panic=-1", 0};
    if (valid)
    {
        // Retain the previously failing CPU cell as a diagnostic only.
        for (size_t i = 0; boot[i]; ++i)
        {
            if (strcmp(boot[i], "-cpu") == 0)
            {
                boot[i + 1] = "qemu64";
            }
            if (strcmp(boot[i], "-d") == 0)
            {
                boot[i + 1] = "int,guest_errors,cpu_reset";
            }
        }
        kernel_run("reference-boot-qemu64-diagnostic", context->source, boot, 3);
        for (size_t i = 0; boot[i]; ++i)
        {
            if (strcmp(boot[i], "-cpu") == 0)
            {
                boot[i + 1] = "max";
            }
            if (strcmp(boot[i], "-d") == 0)
            {
                boot[i + 1] = "guest_errors,cpu_reset";
            }
        }
    }
    valid = valid && kernel_run("reference-boot", context->source, boot, 90) == 0 &&
            kernel_path(log, context->output, "reference-boot.log") &&
            kernel_contains(log, "LINUX_CONTROL_SUMMARY attempted=7 passed=7 failed=0 skipped=0") &&
            kernel_contains(log, "Power down") && !kernel_contains(log, "LINUX_CONTROL_POWEROFF_FAILED");
    if (valid)
    {
        char* candidate_configure[] = {"make", "-C", context->source, candidate, "ARCH=x86",
                                      buster, host, config, "V=1", "allnoconfig", 0};
        int candidate_status = kernel_run("buster-configure", context->source, candidate_configure, 60);
        char* version_probe[] = {context->compiler, "cc", "--version", 0};
        char* preprocessing_probe[] = {context->compiler, "cc", "-E", "-P", "-x", "c", "/dev/null", 0};
        // Reductions expose diagnostics hidden by Kbuild's probe redirection.
        kernel_run("buster-driver-version", context->candidate, version_probe, 15);
        kernel_run("buster-driver-preprocessing", context->candidate, preprocessing_probe, 15);
        // A refused configure is a retained blocker, never a manufactured pass.
        char facts[256], path[KERNEL_PATH_BYTES];
        int bytes = snprintf(facts, sizeof(facts), "reference_boot=pass\nreference_controls=7/7\n"
                             "buster_configure_exit=%d\nbuster_kernel_build=unattempted\n"
                             "buster_kernel_boot=unattempted\nupstream_subsystem_tests=unattempted\n",
                             candidate_status);
        valid = bytes > 0 && bytes < (int)sizeof(facts) && kernel_path(path, context->output, "stages.txt") &&
                kernel_write(path, facts, (size_t)bytes) && kernel_replay() == 0;
    }
    // Always runs, so drift caused by a failed stage is still recorded.
    bool clean_after = kernel_source_verify(context->source, KERNEL_PIN, "source-after");
    valid = valid && clean_after;
    int result = valid ? 0 : 1;
    return result;
}

BUSTER_GLOBAL_LOCAL bool kernel_index_control(char const* path)
{
    FILE* file = fopen(path, "wb");
    bool valid = file != 0;
    if (file)
    {
        valid = kernel_index_row(file, "pending.argv", "pending.o", false, -1) &&
                kernel_index_row(file, "success.argv", "success.o", true, 0) &&
                kernel_index_row(file, "failure.argv", "failure.s", true, 37);
        valid = fclose(file) == 0 && valid;
    }
    file = valid ? fopen(path, "rb") : 0;
    valid = file != 0 && valid;
    if (file)
    {
        char record[128], output[128], state[16];
        int status = 0;
        unsigned rows = 0, pending = 0, passed = 0, failed = 0;
        while (fscanf(file, "%127[^\t]\t%127[^\t]\t%15[^\t]\t%d\n", record, output, state, &status) == 4)
        {
            ++rows;
            pending += strcmp(state, "unattempted") == 0 && status == -1 ? 1u : 0u;
            passed += strcmp(state, "attempted") == 0 && status == 0 ? 1u : 0u;
            failed += strcmp(state, "attempted") == 0 && status == 37 ? 1u : 0u;
        }
        valid = feof(file) && !ferror(file) && rows == 3 && pending == 1 && passed == 1 && failed == 1;
        valid = fclose(file) == 0 && valid;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_control_git(char const* name, char const* repository, char** arguments)
{
    bool valid = kernel_run(name, repository, arguments, 30) == 0;
    return valid;
}

// Source-pin controls on a disposable Git fixture: each drift the review
// reproduced (index flags, ignored/untracked files, mode, replacement refs,
// inherited GIT_* overrides, missing paths) must be refused, and the pristine
// fixture must still pass afterwards.
BUSTER_GLOBAL_LOCAL bool kernel_source_controls(char const* temporary)
{
    char repository[KERNEL_PATH_BYTES], fixture[KERNEL_PATH_BYTES], tool[KERNEL_PATH_BYTES], link[KERNEL_PATH_BYTES];
    char injected[KERNEL_PATH_BYTES], exclude[KERNEL_PATH_BYTES], pin[128] = {0};
    char const pristine[] = "int pristine;\n";
    char const altered[] = "int altered;\n";
    bool valid = kernel_git_environment() && kernel_path(repository, temporary, "repository") &&
                 mkdir(repository, 0700) == 0 && kernel_path(fixture, repository, "fixture.c") &&
                 kernel_path(tool, repository, "tool.sh") && kernel_path(link, repository, "link") &&
                 kernel_path(injected, repository, "injected.h") &&
                 kernel_path(exclude, repository, ".git/info/exclude") &&
                 kernel_write(fixture, pristine, sizeof(pristine) - 1) && kernel_write(tool, "#!/bin/sh\n", 10) &&
                 chmod(tool, 0755) == 0 && symlink("fixture.c", link) == 0;
    char* init[] = {"git", "init", "-q", 0};
    char* add[] = {"git", "add", "-A", 0};
    char* commit[] = {"git", "-c", "user.name=control", "-c", "user.email=control@invalid", "-c", "commit.gpgsign=false",
        "commit", "-q", "-a", "-m", "fixture", 0};
    char* head[] = {"git", "rev-parse", "HEAD", 0};
    valid = valid && kernel_control_git("control-git-init", repository, init) &&
            kernel_control_git("control-git-add", repository, add) &&
            kernel_control_git("control-git-commit", repository, commit) &&
            kernel_control_git("control-git-head", repository, head);
    size_t size = 0;
    char* bytes = valid ? kernel_load_log("control-git-head", &size) : 0;
    valid = bytes && size > 1 && size < sizeof(pin) && bytes[size - 1] == '\n';
    if (valid)
    {
        memcpy(pin, bytes, size - 1);
    }
    free(bytes);
    valid = valid && kernel_source_verify(repository, pin, "control-pristine");
    // assume-unchanged and skip-worktree hide a tracked edit from git status.
    char* assume[] = {"git", "update-index", "--assume-unchanged", "fixture.c", 0};
    char* unassume[] = {"git", "update-index", "--no-assume-unchanged", "fixture.c", 0};
    char* skip[] = {"git", "update-index", "--skip-worktree", "fixture.c", 0};
    char* unskip[] = {"git", "update-index", "--no-skip-worktree", "fixture.c", 0};
    valid = valid && kernel_control_git("control-git-assume", repository, assume) &&
            kernel_write(fixture, altered, sizeof(altered) - 1) &&
            !kernel_source_verify(repository, pin, "control-assume-unchanged") &&
            kernel_control_git("control-git-unassume", repository, unassume) &&
            kernel_control_git("control-git-skip", repository, skip) &&
            !kernel_source_verify(repository, pin, "control-skip-worktree") &&
            kernel_control_git("control-git-unskip", repository, unskip) &&
            kernel_write(fixture, pristine, sizeof(pristine) - 1);
    // An ignored extra input, an executable-bit change and a missing path.
    valid = valid && kernel_write(exclude, "injected.h\n", 11) && kernel_write(injected, "#define X 1\n", 12) &&
            !kernel_source_verify(repository, pin, "control-ignored") && unlink(injected) == 0 &&
            chmod(fixture, 0755) == 0 && !kernel_source_verify(repository, pin, "control-mode") &&
            chmod(fixture, 0644) == 0 && unlink(tool) == 0 && !kernel_source_verify(repository, pin, "control-missing") &&
            kernel_write(tool, "#!/bin/sh\n", 10) && chmod(tool, 0755) == 0;
    // A replacement ref that makes the altered bytes look like the pin.
    char* alternate[] = {"git", "-c", "user.name=control", "-c", "user.email=control@invalid", "-c",
        "commit.gpgsign=false", "commit", "-q", "-a", "-m", "replacement", 0};
    char* reset[] = {"git", "reset", "-q", "--hard", pin, 0};
    char replacement[128] = {0};
    valid = valid && kernel_write(fixture, altered, sizeof(altered) - 1) &&
            kernel_control_git("control-git-alternate", repository, alternate) &&
            kernel_control_git("control-git-alternate-head", repository, head);
    bytes = valid ? kernel_load_log("control-git-alternate-head", &size) : 0;
    valid = bytes && size > 1 && size < sizeof(replacement) && bytes[size - 1] == '\n';
    if (valid)
    {
        memcpy(replacement, bytes, size - 1);
    }
    free(bytes);
    char* replace[] = {"git", "replace", pin, replacement, 0};
    char* unreplace[] = {"git", "replace", "-d", pin, 0};
    valid = valid && kernel_control_git("control-git-reset", repository, reset) &&
            kernel_control_git("control-git-replace", repository, replace) &&
            kernel_write(fixture, altered, sizeof(altered) - 1) &&
            !kernel_source_verify(repository, pin, "control-replace") &&
            kernel_control_git("control-git-unreplace", repository, unreplace) &&
            kernel_write(fixture, pristine, sizeof(pristine) - 1);
    // Inherited overrides must be cleared rather than redirect verification.
    valid = valid && setenv("GIT_DIR", temporary, 1) == 0 && setenv("GIT_INDEX_FILE", injected, 1) == 0 &&
            kernel_source_verify(repository, pin, "control-environment");
    return valid;
}

BUSTER_GLOBAL_LOCAL int kernel_self_test(void)
{
    char temporary[] = "/tmp/buster-kernel-controls-XXXXXX";
    bool valid = mkdtemp(temporary) != 0;
    if (valid)
    {
        strcpy(kernel_context.output, temporary);
        ssize_t bytes = readlink("/proc/self/exe", kernel_context.self, sizeof(kernel_context.self) - 1);
        valid = bytes > 0 && bytes < (ssize_t)sizeof(kernel_context.self) - 1;
        if (valid)
        {
            kernel_context.self[bytes] = 0;
            char* success[] = {kernel_context.self, "control-success", "space in argument", "", 0};
            char* failure[] = {kernel_context.self, "control-failure", 0};
            char* hanging[] = {kernel_context.self, "control-hang", 0};
            valid = kernel_run("control-success", temporary, success, 2) == 0 &&
                    kernel_run("control-failure", temporary, failure, 2) == 37 &&
                    kernel_run("control-hang", temporary, hanging, 1) == 124;
            char path[KERNEL_PATH_BYTES];
            size_t count = 0;
            valid = valid && kernel_path(path, temporary, "control-success.argv") &&
                    kernel_read_record(path, &count) && count == 5 &&
                    strcmp(kernel_arguments[3], "space in argument") == 0 && kernel_arguments[4][0] == 0;
            valid = valid && kernel_path(path, temporary, "malformed.argv") &&
                    kernel_write(path, "unterminated", 12) && !kernel_read_record(path, &count);
            char* fixture[] = {temporary, "gcc", "-O2", "-c", "-o", "kernel.o", "kernel.c", 0};
            valid = valid && kernel_argv(path, temporary, fixture + 1) && kernel_read_record(path, &count) &&
                    kernel_target_c(count);
            fixture[3] = "-S";
            fixture[5] = "kernel.s";
            valid = valid && kernel_argv(path, temporary, fixture + 1) && kernel_read_record(path, &count) &&
                    kernel_target_c(count);
            valid = valid && kernel_path(path, temporary, "ledger.tsv") && kernel_index_control(path);
            fixture[3] = "-E";
            valid = valid && kernel_argv(path, temporary, fixture + 1) && kernel_read_record(path, &count) &&
                    !kernel_target_c(count);
            // Two records from one process (as after PID reuse) must not collide.
            char first[KERNEL_PATH_BYTES], second[KERNEL_PATH_BYTES], result_path[KERNEL_PATH_BYTES];
            valid = valid && kernel_reserve(temporary, "target", first, result_path) &&
                    kernel_reserve(temporary, "target", second, result_path) && strcmp(first, second) != 0;
            valid = valid && kernel_source_controls(temporary);
        }
    }
    printf("LINUX_HARNESS_CONTROLS result=%s directory=%s\n", valid ? "pass" : "fail", temporary);
    int result = valid ? 0 : 1;
    return result;
}

int main(int argc, char** argv)
{
    int result = 2;
    if (argc == 2 && strcmp(argv[1], "self-test") == 0)
    {
        result = kernel_self_test();
    }
    else if (argc == 2 && strcmp(argv[1], "control-success") == 0)
    {
        result = 0;
    }
    else if (argc == 4 && strcmp(argv[1], "control-success") == 0)
    {
        result = strcmp(argv[2], "space in argument") == 0 && argv[3][0] == 0 ? 0 : 1;
    }
    else if (argc == 2 && strcmp(argv[1], "control-failure") == 0)
    {
        result = 37;
    }
    else if (argc == 2 && strcmp(argv[1], "control-hang") == 0)
    {
        for (;;)
        {
            pause();
        }
    }
    else if (argc >= 2 && (strcmp(argv[1], "record-cc") == 0 || strcmp(argv[1], "record-host") == 0 ||
                      strcmp(argv[1], "buster-cc") == 0))
    {
        result = kernel_record(argc, argv, strcmp(argv[1], "record-host") == 0, strcmp(argv[1], "buster-cc") == 0);
    }
    else if (argc == 7 && strcmp(argv[1], "run") == 0)
    {
        KernelContext* context = &kernel_context;
        bool valid = realpath(argv[2], context->source) && realpath(argv[3], context->compiler) &&
                     realpath(argv[5], context->initramfs) && realpath(argv[6], context->config);
        ssize_t length = readlink("/proc/self/exe", context->self, sizeof(context->self) - 1);
        valid = length > 0 && length < (ssize_t)sizeof(context->self) - 1 && valid;
        if (length > 0)
        {
            context->self[length] = 0;
        }
        valid = valid && argv[4][0] == '/' && mkdir(argv[4], 0700) == 0 &&
                realpath(argv[4], context->output);
        valid = valid && !strpbrk(context->self, " \t\n") && !strpbrk(context->source, " \t\n") &&
                !strpbrk(context->output, " \t\n") &&
                kernel_path(context->reference, context->output, "reference") &&
                kernel_path(context->candidate, context->output, "candidate") &&
                kernel_path(context->records, context->output, "commands") &&
                mkdir(context->reference, 0700) == 0 && mkdir(context->candidate, 0700) == 0 &&
                mkdir(context->records, 0700) == 0;
        result = valid ? kernel_frontier(context->config) : 2;
    }
    else
    {
        fprintf(stderr, "usage: kernel-frontier run SOURCE IDE NEW_ABSOLUTE_OUTPUT INITRAMFS FRAGMENT\n");
    }
    return result;
}
