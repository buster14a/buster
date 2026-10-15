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
#define KERNEL_MAX_ACTION_RECORDS 8192
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

typedef struct KernelActionResult
{
    int status;
    char role[32];
    char tool_path[KERNEL_PATH_BYTES];
    char tool_identity[64];
    char output[KERNEL_PATH_BYTES];
    char artifact_identity[128];
} KernelActionResult;

BUSTER_GLOBAL_LOCAL bool kernel_resolve_tool(char const* command, char* resolved)
{
    bool found = false;
    if (strchr(command, '/'))
    {
        found = realpath(command, resolved) != 0;
    }
    else
    {
        char const* path = getenv("PATH");
        size_t path_size = path ? strlen(path) : 0;
        char search[KERNEL_PATH_BYTES];
        bool valid = path && path_size < sizeof(search);
        if (valid)
        {
            memcpy(search, path, path_size + 1);
            char const* cursor = search;
            while (*cursor && !found)
            {
                char const* end = strchr(cursor, ':');
                size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
                char candidate[KERNEL_PATH_BYTES];
                int bytes = length == 0
                                ? snprintf(candidate, sizeof(candidate), "%s", command)
                                : snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)length, cursor, command);
                if (bytes > 0 && bytes < (int)sizeof(candidate) && access(candidate, X_OK) == 0)
                {
                    found = realpath(candidate, resolved) != 0;
                }
                cursor = end ? end + 1 : cursor + length;
            }
        }
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool kernel_action_metadata(char const* cwd, char* const* arguments,
                                                  size_t count, char* output, size_t output_size,
                                                  char* artifact, size_t artifact_size)
{
    char const* selected = 0;
    size_t start = count > 1 && strcmp(arguments[1], "cc") == 0 ? 2 : 1;
    for (size_t i = start; i < count; ++i)
    {
        if (strcmp(arguments[i], "-o") == 0 && i + 1 < count)
        {
            selected = arguments[++i];
        }
        else if (strncmp(arguments[i], "-o", 2) == 0 && arguments[i][2] != 0)
        {
            selected = arguments[i] + 2;
        }
    }
    bool valid = true;
    output[0] = 0;
    strcpy(artifact, "missing");
    if (selected)
    {
        int bytes = selected[0] == '/'
                        ? snprintf(output, output_size, "%s", selected)
                        : snprintf(output, output_size, "%s/%s", cwd, selected);
        valid = bytes > 0 && (size_t)bytes < output_size;
        struct stat facts;
        if (valid && stat(output, &facts) == 0)
        {
            bytes = snprintf(artifact, artifact_size, "stat:%llu:%llu:%llu",
                             (unsigned long long)facts.st_dev, (unsigned long long)facts.st_ino,
                             (unsigned long long)facts.st_size);
            valid = bytes > 0 && (size_t)bytes < artifact_size;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_record_output(char const* cwd, char const* output,
                                                    char* expected, size_t expected_size)
{
    bool valid = cwd && output && expected && expected_size > 0 &&
                 cwd[0] == '/' && !strpbrk(cwd, "\t\r\n") &&
                 !strpbrk(output, "\t\r\n");
    if (valid && output[0] == 0)
    {
        expected[0] = 0;
    }
    else if (valid)
    {
        int bytes = output[0] == '/'
                        ? snprintf(expected, expected_size, "%s", output)
                        : snprintf(expected, expected_size, "%s/%s", cwd, output);
        valid = bytes > 0 && (size_t)bytes < expected_size;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_artifact_identity_valid(char const* identity)
{
    bool valid = strcmp(identity, "missing") == 0;
    if (!valid && strncmp(identity, "stat:", 5) == 0)
    {
        char const* cursor = identity + 5;
        valid = true;
        for (unsigned field = 0; field < 3 && valid; ++field)
        {
            char const* first = cursor;
            while (*cursor >= '0' && *cursor <= '9')
            {
                ++cursor;
            }
            valid = cursor != first;
            if (field < 2)
            {
                valid = valid && *cursor == ':';
                if (valid)
                {
                    ++cursor;
                }
            }
        }
        valid = valid && *cursor == 0;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_result_write(char const* path, int status, char const* role,
                                               char const* tool_path, char const* output,
                                               char const* artifact_identity)
{
    char facts[2 * KERNEL_PATH_BYTES + 512];
    char const* identity = strcmp(tool_path, "-") == 0 ? "unresolved" : "resolved-path-only";
    int bytes = snprintf(facts, sizeof(facts),
                         "exit=%d\nrole=%s\ntool_path=%s\ntool_identity=%s\n"
                         "output=%s\nartifact_identity=%s\n",
                         status, role, tool_path, identity, output[0] ? output : "-", artifact_identity);
    bool valid = bytes > 0 && bytes < (int)sizeof(facts) &&
                 !strpbrk(tool_path, "\t\r\n") && !strpbrk(output, "\t\r\n") &&
                 kernel_write(path, facts, (size_t)bytes);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_result_read(char const* path, KernelActionResult* result)
{
    memset(result, 0, sizeof(*result));
    result->status = -1;
    FILE* file = fopen(path, "rb");
    bool valid = file != 0;
    char line[2 * KERNEL_PATH_BYTES + 256];
    if (file)
    {
        for (unsigned field = 0; field < 6 && valid; ++field)
        {
            valid = fgets(line, sizeof(line), file) != 0;
            size_t length = valid ? strlen(line) : 0;
            valid = valid && length > 0 && line[length - 1] == '\n';
            if (valid)
            {
                line[length - 1] = 0;
                if (field == 0)
                {
                    char* end = 0;
                    long value = strtol(line + (strncmp(line, "exit=", 5) == 0 ? 5 : 0), &end, 10);
                    valid = strncmp(line, "exit=", 5) == 0 && end != line + 5 && *end == 0 &&
                            value >= 0 && value <= 255;
                    result->status = valid ? (int)value : -1;
                }
                else if (field == 1)
                {
                    valid = strncmp(line, "role=", 5) == 0 && strlen(line + 5) < sizeof(result->role);
                    if (valid)
                    {
                        strcpy(result->role, line + 5);
                        valid = strcmp(result->role, "trusted-target") == 0 ||
                                strcmp(result->role, "trusted-host") == 0 ||
                                strcmp(result->role, "buster-target") == 0;
                    }
                }
                else if (field == 2)
                {
                    valid = strncmp(line, "tool_path=", 10) == 0 &&
                            strlen(line + 10) < sizeof(result->tool_path);
                    if (valid) strcpy(result->tool_path, line + 10);
                }
                else if (field == 3)
                {
                    valid = strncmp(line, "tool_identity=", 14) == 0 &&
                            (strcmp(line + 14, "resolved-path-only") == 0 ||
                             strcmp(line + 14, "unresolved") == 0);
                    if (valid) strcpy(result->tool_identity, line + 14);
                }
                else if (field == 4)
                {
                    valid = strncmp(line, "output=", 7) == 0 &&
                            strlen(line + 7) < sizeof(result->output);
                    if (valid) strcpy(result->output, line + 7);
                }
                else
                {
                    valid = strncmp(line, "artifact_identity=", 18) == 0 &&
                            strlen(line + 18) < sizeof(result->artifact_identity);
                    if (valid) strcpy(result->artifact_identity, line + 18);
                }
            }
        }
        valid = valid && fgetc(file) == EOF && !ferror(file);
        valid = fclose(file) == 0 && valid;
    }
    bool path_identity_valid = strcmp(result->tool_path, "-") == 0
                                   ? strcmp(result->tool_identity, "unresolved") == 0
                                   : result->tool_path[0] == '/' &&
                                     strcmp(result->tool_identity, "resolved-path-only") == 0;
    bool output_valid = strcmp(result->output, "-") == 0 ||
                        (result->output[0] == '/' && !strpbrk(result->output, "\t\r\n"));
    bool tool_path_valid = !strpbrk(result->tool_path, "\t\r\n");
    bool artifact_valid = kernel_artifact_identity_valid(result->artifact_identity);
    valid = valid && path_identity_valid && output_valid && tool_path_valid && artifact_valid;
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
                char resolved_tool[KERNEL_PATH_BYTES], recorded_tool[KERNEL_PATH_BYTES];
                char output[KERNEL_PATH_BYTES], artifact_identity[128];
                bool tool_resolved = kernel_resolve_tool(kernel_arguments[0], resolved_tool);
                strcpy(recorded_tool, tool_resolved ? resolved_tool : "-");
                bool metadata_valid = kernel_action_metadata(cwd, kernel_arguments, count, output,
                                                             sizeof(output), artifact_identity,
                                                             sizeof(artifact_identity));
                bool result_written = metadata_valid &&
                    kernel_result_write(result_path, result,
                                        buster ? "buster-target" : host ? "trusted-host" : "trusted-target",
                                        recorded_tool, output, artifact_identity);
                if (!result_written)
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

typedef enum KernelActionKind
{
    KERNEL_ACTION_NON_SOURCE,
    KERNEL_ACTION_OTHER_SOURCE,
    KERNEL_ACTION_C_PROBE,
    KERNEL_ACTION_C_OBJECT,
    KERNEL_ACTION_C_ASSEMBLY,
    KERNEL_ACTION_CPP_ASSEMBLY,
    KERNEL_ACTION_RAW_ASSEMBLY,
    KERNEL_ACTION_AMBIGUOUS
} KernelActionKind;

typedef struct KernelAction
{
    KernelActionKind kind;
    char const* input;
    char const* output;
} KernelAction;

BUSTER_GLOBAL_LOCAL bool kernel_suffix(char const* value, char const* suffix)
{
    size_t value_length = strlen(value), suffix_length = strlen(suffix);
    bool matches = value_length >= suffix_length &&
                   strcmp(value + value_length - suffix_length, suffix) == 0;
    return matches;
}

BUSTER_GLOBAL_LOCAL char const* kernel_action_name(KernelActionKind kind)
{
    char const* name = "non-source";
    switch (kind)
    {
        case KERNEL_ACTION_OTHER_SOURCE: name = "other-source-or-input"; break;
        case KERNEL_ACTION_C_PROBE: name = "c-probe"; break;
        case KERNEL_ACTION_C_OBJECT: name = "c-object"; break;
        case KERNEL_ACTION_C_ASSEMBLY: name = "c-generated-assembly"; break;
        case KERNEL_ACTION_CPP_ASSEMBLY: name = "preprocessed-assembly"; break;
        case KERNEL_ACTION_RAW_ASSEMBLY: name = "raw-assembly"; break;
        case KERNEL_ACTION_AMBIGUOUS: name = "ambiguous"; break;
        default: break;
    }
    return name;
}

BUSTER_GLOBAL_LOCAL bool kernel_non_source_operand(char const* argument)
{
    static char const* suffixes[] = {
        ".o", ".obj", ".a", ".so", ".ko", ".mod", ".cmd", ".d", ".lds", ".ld",
        ".map", ".symvers", ".order", ".bin", ".elf"
    };
    bool known = false;
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]) && !known; ++i)
    {
        known = kernel_suffix(argument, suffixes[i]);
    }
    return known;
}

BUSTER_GLOBAL_LOCAL KernelAction kernel_classify_action(size_t count)
{
    enum { KERNEL_MODE_NONE, KERNEL_MODE_OBJECT, KERNEL_MODE_ASSEMBLY, KERNEL_MODE_PREPROCESS };
    bool compile_only = false, stop_after_assembly = false, preprocess_only = false;
    bool syntax_probe = false, response_input = false;
    char const* language = 0;
    char const* action_language = 0;
    char const* input = 0;
    char const* output = 0;
    size_t source_count = 0, unknown_input_count = 0;
    size_t first_argument = count > 2 && strcmp(kernel_arguments[2], "cc") == 0 ? 3 : 2;
    for (size_t i = first_argument; i < count; ++i)
    {
        char const* argument = kernel_arguments[i];
        if (strcmp(argument, "-c") == 0)
        {
            compile_only = true;
        }
        else if (strcmp(argument, "-S") == 0)
        {
            stop_after_assembly = true;
        }
        else if (strcmp(argument, "-E") == 0)
        {
            preprocess_only = true;
        }
        else if (strcmp(argument, "-fsyntax-only") == 0 || strcmp(argument, "-M") == 0 ||
                 strcmp(argument, "-MM") == 0)
        {
            syntax_probe = true;
        }
        else if (strcmp(argument, "-x") == 0 && i + 1 < count)
        {
            char const* selected_language = kernel_arguments[++i];
            language = strcmp(selected_language, "none") == 0 ? 0 : selected_language;
        }
        else if (strncmp(argument, "-x", 2) == 0 && argument[2] != 0)
        {
            language = strcmp(argument + 2, "none") == 0 ? 0 : argument + 2;
        }
        else if (strcmp(argument, "-o") == 0 && i + 1 < count)
        {
            output = kernel_arguments[++i];
        }
        else if (strncmp(argument, "-o", 2) == 0 && argument[2] != 0)
        {
            output = argument + 2;
        }
        else if (strcmp(argument, "-I") == 0 || strcmp(argument, "-D") == 0 ||
                 strcmp(argument, "-U") == 0 || strcmp(argument, "-include") == 0 ||
                 strcmp(argument, "-imacros") == 0 || strcmp(argument, "-isystem") == 0 ||
                 strcmp(argument, "-iquote") == 0 || strcmp(argument, "-idirafter") == 0 ||
                 strcmp(argument, "-MF") == 0 || strcmp(argument, "-MT") == 0 ||
                 strcmp(argument, "-MQ") == 0 || strcmp(argument, "-B") == 0)
        {
            ++i;
        }
        else if (argument[0] == '@')
        {
            response_input = true;
            if (!input)
            {
                input = argument;
            }
        }
        else if ((argument[0] != '-' || strcmp(argument, "-") == 0) &&
                 (!kernel_non_source_operand(argument) || language))
        {
            bool suffix_c = kernel_suffix(argument, ".c");
            bool suffix_preprocessed_c = kernel_suffix(argument, ".i");
            bool suffix_preprocessed_assembly = kernel_suffix(argument, ".S");
            bool suffix_raw_assembly = kernel_suffix(argument, ".s");
            bool recognized_suffix = suffix_c || suffix_preprocessed_c ||
                                     suffix_preprocessed_assembly || suffix_raw_assembly;
            bool recognized_language = language &&
                (strcmp(language, "c") == 0 || strcmp(language, "cpp-output") == 0 ||
                 strcmp(language, "assembler-with-cpp") == 0 || strcmp(language, "assembler") == 0);
            if (recognized_suffix || recognized_language)
            {
                ++source_count;
                if (source_count == 1)
                {
                    input = argument;
                    action_language = language ? language :
                                      suffix_c ? "c" :
                                      suffix_preprocessed_c ? "cpp-output" :
                                      suffix_preprocessed_assembly ? "assembler-with-cpp" : "assembler";
                }
            }
            else
            {
                ++unknown_input_count;
                if (!input)
                {
                    input = argument;
                }
            }
        }
    }
    int mode = preprocess_only ? KERNEL_MODE_PREPROCESS :
               stop_after_assembly ? KERNEL_MODE_ASSEMBLY :
               compile_only ? KERNEL_MODE_OBJECT : KERNEL_MODE_NONE;
    KernelAction action = {KERNEL_ACTION_NON_SOURCE, input ? input : "", output ? output : ""};
    if (response_input || source_count > 1 || unknown_input_count > 1 ||
        (source_count > 0 && unknown_input_count > 0))
    {
        action.kind = KERNEL_ACTION_AMBIGUOUS;
    }
    else if (source_count == 1)
    {
        if (strcmp(action_language, "c") == 0 || strcmp(action_language, "cpp-output") == 0)
        {
            action.kind = mode == KERNEL_MODE_PREPROCESS || syntax_probe || mode == KERNEL_MODE_NONE
                              ? KERNEL_ACTION_C_PROBE
                              : mode == KERNEL_MODE_OBJECT ? KERNEL_ACTION_C_OBJECT : KERNEL_ACTION_C_ASSEMBLY;
        }
        else if (strcmp(action_language, "assembler-with-cpp") == 0)
        {
            action.kind = mode == KERNEL_MODE_PREPROCESS || syntax_probe || mode == KERNEL_MODE_NONE
                              ? KERNEL_ACTION_OTHER_SOURCE : KERNEL_ACTION_CPP_ASSEMBLY;
        }
        else if (strcmp(action_language, "assembler") == 0)
        {
            action.kind = mode == KERNEL_MODE_PREPROCESS || syntax_probe || mode == KERNEL_MODE_NONE
                              ? KERNEL_ACTION_OTHER_SOURCE : KERNEL_ACTION_RAW_ASSEMBLY;
        }
        else
        {
            action.kind = KERNEL_ACTION_OTHER_SOURCE;
        }
    }
    else if (unknown_input_count == 1)
    {
        action.kind = KERNEL_ACTION_OTHER_SOURCE;
    }
    return action;
}

BUSTER_GLOBAL_LOCAL bool kernel_target_c(size_t count)
{
    KernelAction action = kernel_classify_action(count);
    bool object = action.kind == KERNEL_ACTION_C_OBJECT && kernel_suffix(action.output, ".o");
    bool assembly = action.kind == KERNEL_ACTION_C_ASSEMBLY && kernel_suffix(action.output, ".s");
    bool valid = action.output[0] != 0 && (object || assembly) &&
                 strstr(action.output, ".tmp") == 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_target_c_replayable(size_t count)
{
    KernelAction action = kernel_classify_action(count);
    bool valid = kernel_target_c(count) && strcmp(action.input, "-") != 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_index_row(FILE* index, char const* record, char const* output, bool attempted, int status)
{
    int bytes = fprintf(index, "%s\t%s\t%s\t%d\n", record, output, attempted ? "attempted" : "unattempted", status);
    bool valid = bytes > 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL int kernel_record_name_compare(void const* left, void const* right)
{
    char const* const* first = left;
    char const* const* second = right;
    int order = strcmp(*first, *second);
    return order;
}

BUSTER_GLOBAL_LOCAL bool kernel_action_census(void)
{
    char index_path[KERNEL_PATH_BYTES], summary_path[KERNEL_PATH_BYTES];
    bool valid = kernel_path(index_path, kernel_context.output, "actions.tsv");
    FILE* index = valid ? fopen(index_path, "wb") : 0;
    DIR* directory = index ? opendir(kernel_context.records) : 0;
    char* names[KERNEL_MAX_ACTION_RECORDS];
    size_t name_count = 0;
    unsigned classes[8] = {0};
    unsigned reference_records = 0, host_records = 0, candidate_records = 0;
    unsigned reference_c = 0, reference_generated_assembly = 0, reference_assembly_inputs = 0;
    unsigned candidate_c = 0, candidate_generated_assembly = 0, candidate_assembly_inputs = 0;
    unsigned host_assembly_inputs = 0, successful_results = 0, failed_results = 0;
    unsigned result_unknown = 0, artifact_identity_count = 0, malformed_records = 0;
    if (index)
    {
        valid = fprintf(index, "record\tscope\tclass\tinput\targv_output\tresult_output\tresult\ttool_path\ttool_identity\tartifact_identity\n") > 0;
    }
    if (directory)
    {
        struct dirent* entry;
        while ((entry = readdir(directory)) != 0 && valid)
        {
            size_t length = strlen(entry->d_name);
            bool action_record = length > 9 && strcmp(entry->d_name + length - 5, ".argv") == 0 &&
                                 (strncmp(entry->d_name, "target-", 7) == 0 ||
                                  strncmp(entry->d_name, "host-", 5) == 0 ||
                                  strncmp(entry->d_name, "buster-", 7) == 0);
            if (action_record)
            {
                if (name_count == KERNEL_MAX_ACTION_RECORDS)
                {
                    valid = false;
                }
                else
                {
                    size_t name_length = strlen(entry->d_name);
                    names[name_count] = malloc(name_length + 1);
                    valid = names[name_count] != 0;
                    if (valid)
                    {
                        memcpy(names[name_count], entry->d_name, name_length + 1);
                        ++name_count;
                    }
                }
            }
        }
        valid = closedir(directory) == 0 && valid;
    }
    else
    {
        valid = false;
    }
    if (valid)
    {
        qsort(names, name_count, sizeof(names[0]), kernel_record_name_compare);
        for (size_t row = 0; row < name_count && valid; ++row)
        {
            char record_path[KERNEL_PATH_BYTES], result_path[KERNEL_PATH_BYTES];
            char expected_output[KERNEL_PATH_BYTES];
            size_t count = 0, length = strlen(names[row]);
            KernelAction action = {KERNEL_ACTION_NON_SOURCE, "", ""};
            KernelActionResult result;
            memset(&result, 0, sizeof(result));
            bool record_valid = kernel_path(record_path, kernel_context.records, names[row]) &&
                                kernel_read_record(record_path, &count);
            if (record_valid)
            {
                action = kernel_classify_action(count);
                record_valid = !strpbrk(action.input, "\t\r\n") &&
                               !strpbrk(action.output, "\t\r\n");
                if (!record_valid)
                {
                    ++malformed_records;
                }
            }
            else
            {
                ++malformed_records;
            }
            memcpy(result_path, names[row], length + 1);
            if (length >= 5)
            {
                strcpy(result_path + length - 5, ".result");
            }
            char const* expected_role = strncmp(names[row], "target-", 7) == 0 ? "trusted-target" :
                                        strncmp(names[row], "host-", 5) == 0 ? "trusted-host" : "buster-target";
            if (strcmp(expected_role, "trusted-target") == 0)
            {
                ++reference_records;
            }
            else if (strcmp(expected_role, "trusted-host") == 0)
            {
                ++host_records;
            }
            else
            {
                ++candidate_records;
            }
            bool expected_output_valid = record_valid &&
                kernel_record_output(kernel_arguments[0], action.output,
                                     expected_output, sizeof(expected_output));
            bool result_valid = record_valid && expected_output_valid &&
                                kernel_path(record_path, kernel_context.records, result_path) &&
                                kernel_result_read(record_path, &result);
            result_valid = result_valid && strcmp(result.role, expected_role) == 0 &&
                           strcmp(result.output, expected_output[0] ? expected_output : "-") == 0;
            if (!result_valid)
            {
                ++result_unknown;
            }
            else
            {
                if (result.status == 0)
                {
                    ++successful_results;
                }
                else
                {
                    ++failed_results;
                }
                if (strcmp(result.artifact_identity, "missing") != 0)
                {
                    ++artifact_identity_count;
                }
            }
            char const* scope = result_valid ? result.role : expected_role;
            if (record_valid)
            {
                unsigned kind = (unsigned)action.kind;
                if (kind < sizeof(classes) / sizeof(classes[0]))
                {
                    ++classes[kind];
                }
                bool c_action = action.kind == KERNEL_ACTION_C_OBJECT ||
                                action.kind == KERNEL_ACTION_C_ASSEMBLY;
                bool assembly_input = action.kind == KERNEL_ACTION_CPP_ASSEMBLY ||
                                      action.kind == KERNEL_ACTION_RAW_ASSEMBLY;
                if (strcmp(scope, "trusted-target") == 0)
                {
                    reference_c += c_action ? 1u : 0u;
                    reference_generated_assembly += action.kind == KERNEL_ACTION_C_ASSEMBLY ? 1u : 0u;
                    reference_assembly_inputs += assembly_input ? 1u : 0u;
                }
                else if (strcmp(scope, "buster-target") == 0)
                {
                    candidate_c += c_action ? 1u : 0u;
                    candidate_generated_assembly += action.kind == KERNEL_ACTION_C_ASSEMBLY ? 1u : 0u;
                    candidate_assembly_inputs += assembly_input ? 1u : 0u;
                }
                else
                {
                    host_assembly_inputs += assembly_input ? 1u : 0u;
                }
            }
            const char* result_output = result_valid ? result.output : "unobserved";
            const char* tool_path = result_valid ? result.tool_path : "unobserved";
            const char* tool_identity = result_valid ? result.tool_identity : "unobserved";
            const char* artifact = result_valid ? result.artifact_identity : "unobserved";
            const char* result_state = !result_valid ? "missing-or-malformed" :
                                       result.status == 0 ? "success" : "failed";
            int bytes = fprintf(index, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
                                names[row], scope,
                                record_valid ? kernel_action_name(action.kind) : "malformed-argv",
                                record_valid ? action.input : "",
                                record_valid ? action.output : "",
                                result_output, result_state, tool_path, tool_identity, artifact);
            valid = bytes > 0;
        }
    }
    for (size_t row = 0; row < name_count; ++row)
    {
        free(names[row]);
    }
    if (index)
    {
        valid = fclose(index) == 0 && valid;
    }
    char summary[1536];
    int bytes = snprintf(summary, sizeof(summary),
                         "wrapped_action_records=%zu\ntrusted_reference_wrapped_actions=%u\n"
                         "trusted_host_wrapped_actions=%u\nbuster_candidate_wrapped_actions=%u\n"
                         "classified_non_source_actions=%u\nclassified_other_source_or_input_actions=%u\n"
                         "classified_c_probe_actions=%u\nclassified_c_object_actions=%u\n"
                         "classified_c_generated_assembly_actions=%u\nclassified_preprocessed_assembly_actions=%u\n"
                         "classified_raw_assembly_actions=%u\nclassified_ambiguous_actions=%u\n"
                         "trusted_reference_wrapped_c_actions=%u\n"
                         "trusted_reference_wrapped_c_generated_assembly_actions=%u\n"
                         "trusted_reference_wrapped_assembly_inputs=%u\n"
                         "buster_candidate_wrapped_c_actions=%u\n"
                         "buster_candidate_wrapped_c_generated_assembly_actions=%u\n"
                         "buster_candidate_wrapped_assembly_inputs=%u\n"
                         "trusted_host_wrapped_assembly_inputs_excluded=%u\n"
                         "successful_wrapper_results=%u\nfailed_wrapper_results=%u\n"
                         "records_with_artifact_stat_identity=%u\n"
                         "missing_or_malformed_results=%u\nmalformed_argv_records=%u\n"
                         "replayed_reference_c_actions=recorded-separately-in-target-c.tsv\n"
                         "candidate_generated_inputs=unqualified\nfinal_image_membership=unobserved\n"
                         "content_hashes=not-computed\n",
                         name_count, reference_records, host_records, candidate_records,
                         classes[KERNEL_ACTION_NON_SOURCE], classes[KERNEL_ACTION_OTHER_SOURCE],
                         classes[KERNEL_ACTION_C_PROBE], classes[KERNEL_ACTION_C_OBJECT],
                         classes[KERNEL_ACTION_C_ASSEMBLY], classes[KERNEL_ACTION_CPP_ASSEMBLY],
                         classes[KERNEL_ACTION_RAW_ASSEMBLY], classes[KERNEL_ACTION_AMBIGUOUS],
                         reference_c, reference_generated_assembly, reference_assembly_inputs,
                         candidate_c, candidate_generated_assembly, candidate_assembly_inputs,
                         host_assembly_inputs, successful_results, failed_results, artifact_identity_count,
                         result_unknown, malformed_records);
    valid = bytes > 0 && bytes < (int)sizeof(summary) &&
            kernel_path(summary_path, kernel_context.output, "action-census.txt") &&
            kernel_write(summary_path, summary, (size_t)bytes) && valid;
    printf("%s", summary);
    return valid && result_unknown == 0 && malformed_records == 0;
}

typedef struct KernelSelection
{
    char record[128];
    char output[KERNEL_PATH_BYTES];
} KernelSelection;

BUSTER_GLOBAL_LOCAL KernelSelection kernel_selection[KERNEL_REPLAY_LIMIT];

BUSTER_GLOBAL_LOCAL bool kernel_replay_command(size_t count, char* const* recorded,
                                                        char const* destination, char const* dependency,
                                                        char* joined_output, size_t joined_size,
                                                        char** arguments, size_t argument_capacity,
                                                        size_t* argument_count)
{
    bool valid = count >= 2 && recorded && destination && dependency && joined_output &&
                 joined_size > 0 && arguments && argument_capacity >= 3 &&
                 argument_count && !strpbrk(destination, "\t\r\n") &&
                 !strpbrk(dependency, "\t\r\n");
    size_t n = 0;
    if (valid)
    {
        arguments[n++] = kernel_context.compiler;
        arguments[n++] = "cc";
    }
    for (size_t i = 2; i < count && valid; ++i)
    {
        char const* argument = recorded[i];
        if (strcmp(argument, "-o") == 0)
        {
            valid = i + 1 < count && n + 2 < argument_capacity;
            if (valid)
            {
                arguments[n++] = "-o";
                arguments[n++] = (char*)destination;
                ++i;
            }
        }
        else if (strncmp(argument, "-o", 2) == 0 && argument[2] != 0)
        {
            int bytes = snprintf(joined_output, joined_size, "-o%s", destination);
            valid = bytes > 0 && (size_t)bytes < joined_size && n + 1 < argument_capacity;
            if (valid)
            {
                arguments[n++] = joined_output;
            }
        }
        else if (strncmp(argument, "-Wp,-MMD,", 9) == 0)
        {
            valid = n + 1 < argument_capacity;
            if (valid)
            {
                arguments[n++] = (char*)dependency;
            }
        }
        else
        {
            valid = n + 1 < argument_capacity;
            if (valid)
            {
                arguments[n++] = recorded[i];
            }
        }
    }
    if (valid && n < argument_capacity)
    {
        arguments[n] = 0;
        *argument_count = n;
    }
    else
    {
        valid = false;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int kernel_replay(void)
{
    char index_path[KERNEL_PATH_BYTES];
    bool valid = kernel_path(index_path, kernel_context.output, "target-c.tsv");
    FILE* index = valid ? fopen(index_path, "wb") : 0;
    DIR* directory = index ? opendir(kernel_context.records) : 0;
    unsigned required = 0, selected = 0, attempted = 0, passed = 0;
    unsigned malformed = 0, unreplayable_stdin = 0;
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
                    KernelAction action = kernel_classify_action(count);
                    char const* original_output = action.output;
                    if (strcmp(action.input, "-") == 0)
                    {
                        ++unreplayable_stdin;
                    }
                    else
                    {
                        // Stable selection by original object path, independent
                        // of filesystem enumeration and compiler process IDs.
                        unsigned slot = 0;
                        while (slot < selected && strcmp(kernel_selection[slot].output, original_output) <= 0)
                        {
                            ++slot;
                        }
                        if (slot < KERNEL_REPLAY_LIMIT &&
                            strlen(entry->d_name) < sizeof(kernel_selection[slot].record) &&
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
            char joined_output[KERNEL_PATH_BYTES + 2];
            snprintf(name, sizeof(name), "replay-%u", row);
            size_t size = strlen(kernel_selection[row].output);
            valid = size > 0 && size < sizeof(kernel_selection[row].output);
            int leaf_bytes = valid
                                 ? snprintf(leaf, sizeof(leaf), "replay-%u.%c", row,
                                            kernel_selection[row].output[size - 1])
                                 : -1;
            valid = leaf_bytes > 0 && leaf_bytes < (int)sizeof(leaf) &&
                    kernel_path(destination, kernel_context.output, leaf) && valid;
            int bytes = snprintf(dependency, sizeof(dependency), "-Wp,-MMD,%s/replay-%u.d", kernel_context.output, row);
            valid = bytes > 0 && bytes < (int)sizeof(dependency) && valid;
            if (valid)
            {
                char* arguments[KERNEL_MAX_ARGUMENTS] = {0};
                size_t n = 0;
                valid = kernel_replay_command(count, kernel_arguments, destination, dependency,
                                              joined_output, sizeof(joined_output), arguments,
                                              KERNEL_MAX_ARGUMENTS, &n);
                if (valid)
                {
                    int status = kernel_run(name, kernel_arguments[0], arguments, 15);
                    ++attempted;
                    passed += status == 0 ? 1u : 0u;
                    valid = kernel_index_row(index, kernel_selection[row].record,
                                             kernel_selection[row].output, true, status) && valid;
                }
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
                         "replay_failed=%u\nreplay_unattempted=%u\nstdin_input_not_captured=%u\n"
                         "malformed_records=%u\ncandidate_generated_inputs=unqualified\n"
                         "qualified_kernel=no\nperformance=unvalidated\n",
                         required, attempted, passed, attempted - passed, required - attempted,
                         unreplayable_stdin, malformed);
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

BUSTER_GLOBAL_LOCAL bool kernel_config_symbol(char const* path, char const* symbol,
                                                    char* status, size_t status_size,
                                                    bool* contradictory)
{
    FILE* file = fopen(path, "rb");
    bool status_capacity = status_size >= sizeof("unknown");
    bool valid = file != 0 && status_capacity, seen = false;
    char line[512], prior[512] = "";
    if (status_capacity)
    {
        strcpy(status, "unknown");
    }
    *contradictory = false;
    if (file)
    {
        while (fgets(line, sizeof(line), file) != 0 && valid)
        {
            size_t length = strlen(line);
            bool terminated = length > 0 && line[length - 1] == '\n';
            if (terminated)
            {
                line[--length] = 0;
            }
            else if (length == sizeof(line) - 1 && !feof(file))
            {
                valid = false;
            }
            size_t symbol_length = strlen(symbol);
            bool matches = strncmp(line, symbol, symbol_length) == 0 &&
                           (line[symbol_length] == '=' || line[symbol_length] == 0);
            char disabled[128];
            int bytes = snprintf(disabled, sizeof(disabled), "# %s is not set", symbol);
            matches = matches || (bytes > 0 && bytes < (int)sizeof(disabled) &&
                                  strcmp(line, disabled) == 0);
            if (matches)
            {
                if (seen && strcmp(prior, line) != 0)
                {
                    *contradictory = true;
                }
                else if (!seen)
                {
                    strcpy(prior, line);
                    seen = true;
                }
                if (strcmp(line, disabled) == 0)
                {
                    strcpy(status, "disabled");
                }
                else
                {
                    char const* value = strchr(line, '=');
                    if (value && (strcmp(value + 1, "y") == 0 || strcmp(value + 1, "m") == 0))
                    {
                        strcpy(status, "enabled");
                    }
                    else
                    {
                        strcpy(status, "unknown");
                    }
                }
            }
        }
        valid = !ferror(file) && fclose(file) == 0 && valid;
    }
    if (!valid && file)
    {
        *contradictory = true;
    }
    if (*contradictory && status_capacity)
    {
        strcpy(status, "unknown");
    }
    return valid && seen;
}

typedef struct KernelMechanism
{
    char const* symbol;
    char const* interpretation;
} KernelMechanism;

BUSTER_GLOBAL_LOCAL bool kernel_mechanism_row(FILE* file, char const* scope, char const* path,
                                                KernelMechanism mechanism, bool* consistent)
{
    char status[16];
    bool contradictory = false;
    bool found = kernel_config_symbol(path, mechanism.symbol, status, sizeof(status), &contradictory);
    bool valid = !contradictory;
    *consistent = *consistent && valid;
    int bytes = fprintf(file, "%s\t%s\t%s\t%s\truntime-unattempted\n",
                        scope, mechanism.symbol, found ? status : "unknown",
                        mechanism.interpretation);
    return bytes > 0 && valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_mechanism_census(void)
{
    static KernelMechanism const mechanisms[] = {
        {"CONFIG_X86_64", "x86 architecture; alternatives and exception-table obligations"},
        {"CONFIG_JUMP_LABEL", "configured jump-label/static-key mechanism; runtime unattempted"},
        {"CONFIG_HAVE_ARCH_JUMP_LABEL", "architecture capability only; callsite use unknown"},
        {"CONFIG_HAVE_ARCH_JUMP_LABEL_RELATIVE", "architecture capability only; callsite use unknown"},
        {"CONFIG_HAVE_STATIC_CALL", "capability only; static-call use unknown"},
        {"CONFIG_HAVE_STATIC_CALL_INLINE", "capability only; inline static-call use unknown"},
        {"CONFIG_HAVE_OBJTOOL", "capability only; tool invocation not established"},
        {"CONFIG_OBJTOOL", "tool configuration only; invocation remains unobserved"},
        {"CONFIG_STACK_VALIDATION", "configuration only; post-link validation unattempted"},
        {"CONFIG_UNWINDER_ORC", "configured unwind path only; runtime unattempted"},
        {"CONFIG_MITIGATION_RETPOLINE", "mitigation configuration only; runtime unattempted"},
        {"CONFIG_MITIGATION_RETHUNK", "mitigation configuration only; runtime unattempted"},
        {"CONFIG_MITIGATION_SLS", "mitigation configuration only; runtime unattempted"},
        {"CONFIG_X86_KERNEL_IBT", "mitigation configuration only; runtime unattempted"},
        {"CONFIG_KPROBES", "dynamic patching configuration only; runtime unattempted"},
        {"CONFIG_FUNCTION_TRACER", "patching configuration only; runtime unattempted"},
        {"CONFIG_DYNAMIC_FTRACE", "patching configuration only; runtime unattempted"},
        {"CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE", "optimization configuration only; codegen identity is separate"},
        {"CONFIG_LTO", "optimization configuration only; link path unobserved"},
        {"CONFIG_CFI_CLANG", "optimization configuration only; link/runtime unattempted"}
    };
    char output_path[KERNEL_PATH_BYTES], coverage_path[KERNEL_PATH_BYTES];
    bool valid = kernel_path(output_path, kernel_context.output, "mechanisms.tsv");
    FILE* file = valid ? fopen(output_path, "wb") : 0;
    valid = file != 0;
    bool consistent = true;
    if (file)
    {
        valid = fprintf(file, "scope\tsymbol\tstatus\tevidence\truntime\n") > 0;
        char reference[KERNEL_PATH_BYTES], candidate[KERNEL_PATH_BYTES];
        valid = kernel_path(reference, kernel_context.reference, ".config") &&
                kernel_path(candidate, kernel_context.candidate, ".config") && valid;
        for (size_t i = 0; i < sizeof(mechanisms) / sizeof(mechanisms[0]) && valid; ++i)
        {
            valid = kernel_mechanism_row(file, "trusted-reference", reference, mechanisms[i],
                                         &consistent) && valid;
            valid = kernel_mechanism_row(file, "buster-candidate", candidate, mechanisms[i],
                                         &consistent) && valid;
        }
        static char const* unresolved[] = {
            "alternative-callsite-use\tsource/use census not performed",
            "exception-table-use\tsource/use census not performed",
            "static-call-callsite-use\tsource/use census not performed",
            "static-call-permanent-trampoline\tuse-dependent obligation remains when inline calls are unavailable",
            "objtool-invocation\tdirect tool invocation not captured",
            "final-image-membership\tlink/output membership not observed"
        };
        static char const* scopes[] = {"trusted-reference", "buster-candidate"};
        for (size_t i = 0; i < sizeof(unresolved) / sizeof(unresolved[0]) && valid; ++i)
        {
            char const* split = strchr(unresolved[i], '\t');
            char item[64], evidence[128];
            size_t item_size = (size_t)(split - unresolved[i]);
            size_t evidence_size = strlen(split + 1);
            valid = item_size < sizeof(item) && evidence_size < sizeof(evidence);
            if (valid)
            {
                memcpy(item, unresolved[i], item_size);
                item[item_size] = 0;
                memcpy(evidence, split + 1, evidence_size + 1);
                for (size_t scope = 0; scope < sizeof(scopes) / sizeof(scopes[0]) && valid; ++scope)
                {
                    int bytes = fprintf(file, "%s\t%s\tunknown\t%s\truntime-unattempted\n",
                                        scopes[scope], item, evidence);
                    valid = bytes > 0;
                }
            }
        }
        valid = fclose(file) == 0 && valid;
    }
    valid = valid && consistent &&
            kernel_path(coverage_path, kernel_context.output, "tool-coverage.tsv");
    FILE* coverage = valid ? fopen(coverage_path, "wb") : 0;
    valid = coverage != 0;
    if (coverage)
    {
        valid = fprintf(coverage,
                        "scope\ttool\tobservation\tidentity_limit\n"
                        "trusted-target\tCC\targv-and-result-recorded\tresolved-path-only; version/hash not verified\n"
                        "trusted-host\tHOSTCC\targv-and-result-recorded\tresolved-path-only; version/hash not verified\n"
                        "buster-target\tCC\targv-and-result-recorded-if-invoked\tresolved-path-only; version/hash not verified\n"
                        "trusted-target\tAS/LD/AR/objcopy/objdump/objtool/post-link\tunobserved\tnot routed through CC/HOSTCC wrappers\n"
                        "trusted-host\tAS/LD/AR/objcopy/objdump/objtool/post-link\tunobserved\tnot routed through CC/HOSTCC wrappers\n"
                        "buster-target\tAS/LD/AR/objcopy/objdump/objtool/post-link\tunobserved\tnot routed through CC/HOSTCC wrappers\n"
                        "trusted-target\tGNU/LLVM standalone assembler\tunobserved\tdirect assembler argv and result are not parsed\n"
                        "buster-target\texternal GNU/LLVM assembler\tunattempted\tno candidate assembly action was executed\n"
                        "buster-target\tnative Buster assembler\tunattempted\tno candidate assembly action was executed\n"
                        "trusted-target\tnested-compiler-children\tunobserved\targv does not establish child executable identity\n"
                        "buster-target\tnested-compiler-children\tunobserved\targv does not establish child executable identity\n"
                        "trusted-target\tfinal-image-object-membership\tunobserved\tno link map or image membership proof\n"
                        "buster-target\tfinal-image-object-membership\tunobserved\tno link map or image membership proof\n") > 0;
        valid = fclose(coverage) == 0 && valid;
    }
    return valid && consistent;
}

BUSTER_GLOBAL_LOCAL bool kernel_config_expect(char const* path, char const* symbol,
                                                 char const* expected)
{
    char status[16];
    bool contradictory = false;
    bool found = kernel_config_symbol(path, symbol, status, sizeof(status), &contradictory);
    bool valid = found && !contradictory && strcmp(status, expected) == 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_config_not_enabled(char const* path, char const* symbol)
{
    char status[16];
    bool contradictory = false;
    bool found = kernel_config_symbol(path, symbol, status, sizeof(status), &contradictory);
    bool valid = !contradictory && (!found || strcmp(status, "disabled") == 0);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_config_matches(void)
{
    char path[KERNEL_PATH_BYTES];
    bool valid = kernel_path(path, kernel_context.reference, ".config");
    char const* enabled[] = {
        "CONFIG_64BIT", "CONFIG_X86_64", "CONFIG_BINFMT_ELF", "CONFIG_BLK_DEV_INITRD",
        "CONFIG_SERIAL_8250_CONSOLE", "CONFIG_TMPFS", "CONFIG_FUTEX", "CONFIG_POSIX_TIMERS",
        "CONFIG_INET", "CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE", "CONFIG_RELOCATABLE",
        "CONFIG_PCI", "CONFIG_ACPI"
    };
    char const* disabled[] = {"CONFIG_MODULES", "CONFIG_RANDOMIZE_BASE"};
    for (size_t i = 0; i < sizeof(enabled) / sizeof(enabled[0]) && valid; ++i)
    {
        valid = kernel_config_expect(path, enabled[i], "enabled");
    }
    for (size_t i = 0; i < sizeof(disabled) / sizeof(disabled[0]) && valid; ++i)
    {
        valid = kernel_config_expect(path, disabled[i], "disabled");
    }
    valid = kernel_config_not_enabled(path, "CONFIG_RUST") && valid;
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
    size_t self_length = strnlen(context->self, sizeof(context->self));
    size_t reference_length = strnlen(context->reference, sizeof(context->reference));
    size_t candidate_length = strnlen(context->candidate, sizeof(context->candidate));
    size_t fragment_length = strnlen(fragment, KERNEL_PATH_BYTES);
    int cc_bytes = snprintf(cc, sizeof(cc), "CC=%.*s record-cc",
                            (int)sizeof(context->self) - 1, context->self);
    int host_bytes = snprintf(host, sizeof(host), "HOSTCC=%.*s record-host",
                              (int)sizeof(context->self) - 1, context->self);
    int buster_bytes = snprintf(buster, sizeof(buster), "CC=%.*s buster-cc",
                                (int)sizeof(context->self) - 1, context->self);
    int reference_bytes = snprintf(reference, sizeof(reference), "O=%.*s",
                                   (int)sizeof(context->reference) - 1, context->reference);
    int candidate_bytes = snprintf(candidate, sizeof(candidate), "O=%.*s",
                                   (int)sizeof(context->candidate) - 1, context->candidate);
    int config_bytes = snprintf(config, sizeof(config), "KCONFIG_ALLCONFIG=%.*s",
                                KERNEL_PATH_BYTES - 1, fragment);
    valid = valid && self_length < sizeof(context->self) &&
            reference_length < sizeof(context->reference) &&
            candidate_length < sizeof(context->candidate) && fragment_length < KERNEL_PATH_BYTES &&
            cc_bytes > 0 && cc_bytes < (int)sizeof(cc) &&
            host_bytes > 0 && host_bytes < (int)sizeof(host) &&
            buster_bytes > 0 && buster_bytes < (int)sizeof(buster) &&
            reference_bytes > 0 && reference_bytes < (int)sizeof(reference) &&
            candidate_bytes > 0 && candidate_bytes < (int)sizeof(candidate) &&
            config_bytes > 0 && config_bytes < (int)sizeof(config);
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
                              "buster_kernel_boot=unattempted\nupstream_subsystem_tests=unattempted\n"
                              "alternative_static_call_runtime=unattempted\nfinal_image_membership=unobserved\n",
                              candidate_status);
        bool mechanisms_recorded = kernel_mechanism_census();
        bool actions_recorded = kernel_action_census();
        int replay_status = kernel_replay();
        valid = bytes > 0 && bytes < (int)sizeof(facts) && kernel_path(path, context->output, "stages.txt") &&
                kernel_write(path, facts, (size_t)bytes) && mechanisms_recorded && actions_recorded &&
                replay_status == 0;
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

BUSTER_GLOBAL_LOCAL bool kernel_classifier_controls(char const* temporary)
{
    char path[KERNEL_PATH_BYTES];
    char* cases[][12] = {
        {"gcc", "-c", "-o", "kernel.o", "kernel.c", 0},
        {"gcc", "-S", "-o", "kernel.s", "kernel.c", 0},
        {"gcc", "-c", "-o", "upper.o", "kernel.S", 0},
        {"gcc", "-c", "-o", "raw.o", "kernel.s", 0},
        {"gcc", "-x", "assembler", "-c", "-o", "forced.o", "kernel.c", 0},
        {"gcc", "-x", "assembler-with-cpp", "-c", "-o", "forced.o", "kernel.s", 0},
        {"gcc", "-x", "c", "-c", "-o", "forced.o", "kernel.S", 0},
        {"gcc", "-x", "assembler", "-x", "none", "-c", "reset.c", "-o", "reset.o", 0},
        {"gcc", "-c", "-S", "-o", "last.s", "kernel.c", 0},
        {"gcc", "-S", "-c", "-o", "last.s", "kernel.c", 0},
        {"gcc", "-E", "-c", "-o", "preprocessed.i", "kernel.c", 0},
        {"gcc", "-fsyntax-only", "-c", "-o", "probe.o", "kernel.c", 0},
        {"gcc", "-c", "kernel.S", "-x", "c", "-o", "late.o", 0},
        {"gcc", "-x", "c", "-c", "-o", "extensionless.o", "extensionless", 0},
        {"gcc", "-xc", "-c", "-o", "preprocessed.o", "preprocessed.i", 0},
        {"gcc", "-c", "-okernel.o", "kernel.c", 0},
        {"gcc", "-S", "-E", "-o", "probe.i", "kernel.c", 0},
        {"gcc", "-c", "-x", "c", "-o", "stdin.o", "-", 0},
        {"gcc", "-x", "c", "-c", "-o", "typed.o", "fixture.o", 0},
        {"gcc", "-x", "assembler", "-c", "-o", "typed.o", "fixture.bin", 0},
        {"gcc", "-c", "-o", "response.o", "@compile.rsp", 0}
    };
    KernelActionKind expected[] = {
        KERNEL_ACTION_C_OBJECT, KERNEL_ACTION_C_ASSEMBLY, KERNEL_ACTION_CPP_ASSEMBLY,
        KERNEL_ACTION_RAW_ASSEMBLY, KERNEL_ACTION_RAW_ASSEMBLY, KERNEL_ACTION_CPP_ASSEMBLY,
        KERNEL_ACTION_C_OBJECT, KERNEL_ACTION_C_OBJECT, KERNEL_ACTION_C_ASSEMBLY,
        KERNEL_ACTION_C_ASSEMBLY, KERNEL_ACTION_C_PROBE, KERNEL_ACTION_C_PROBE,
        KERNEL_ACTION_CPP_ASSEMBLY, KERNEL_ACTION_C_OBJECT, KERNEL_ACTION_C_OBJECT,
        KERNEL_ACTION_C_OBJECT, KERNEL_ACTION_C_PROBE, KERNEL_ACTION_C_OBJECT,
        KERNEL_ACTION_C_OBJECT, KERNEL_ACTION_RAW_ASSEMBLY, KERNEL_ACTION_AMBIGUOUS
    };
    bool expected_target[] = {
        true, true, false, false, false, false, true, true, true, true, false, false,
        false, true, true, true, false, true, true, false, false
    };
    bool expected_replayable[] = {
        true, true, false, false, false, false, true, true, true, true, false, false,
        false, true, true, true, false, false, true, false, false
    };
    bool valid = kernel_path(path, temporary, "classify.argv");
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]) && valid; ++i)
    {
        size_t count = 0;
        valid = kernel_argv(path, temporary, cases[i]) && kernel_read_record(path, &count);
        KernelAction action = valid ? kernel_classify_action(count) :
                                      (KernelAction){KERNEL_ACTION_AMBIGUOUS, "", ""};
        valid = valid && action.kind == expected[i] && kernel_target_c(count) == expected_target[i] &&
                kernel_target_c_replayable(count) == expected_replayable[i];
        if (valid && i == 15)
        {
            valid = strcmp(action.output, "kernel.o") == 0;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_replay_controls(void)
{
    char joined_output[KERNEL_PATH_BYTES + 2];
    char* arguments[KERNEL_MAX_ARGUMENTS] = {0};
    size_t count = 0;
    char* split[] = {
        "/tmp/control", "/usr/bin/gcc", "-o", "original.o",
        "-Wp,-MMD,original.d", "-c", "source.c", 0
    };
    char* joined[] = {
        "/tmp/control", "/usr/bin/gcc", "-ooriginal.o",
        "-Wp,-MMD,original.d", "-c", "source.c", 0
    };
    strcpy(kernel_context.compiler, "/usr/bin/test-compiler");
    bool valid = kernel_replay_command(7, split, "/tmp/replay.o", "-Wp,-MMD,/tmp/replay.d",
                                       joined_output, sizeof(joined_output), arguments,
                                       KERNEL_MAX_ARGUMENTS, &count) &&
                 count == 7 && strcmp(arguments[0], "/usr/bin/test-compiler") == 0 &&
                 strcmp(arguments[1], "cc") == 0 && strcmp(arguments[2], "-o") == 0 &&
                 strcmp(arguments[3], "/tmp/replay.o") == 0 &&
                 strcmp(arguments[4], "-Wp,-MMD,/tmp/replay.d") == 0 &&
                 strcmp(arguments[5], "-c") == 0 && strcmp(arguments[6], "source.c") == 0;
    valid = kernel_replay_command(6, joined, "/tmp/replay.s", "-Wp,-MMD,/tmp/replay.d",
                                  joined_output, sizeof(joined_output), arguments,
                                  KERNEL_MAX_ARGUMENTS, &count) &&
            count == 6 && strcmp(arguments[2], "-o/tmp/replay.s") == 0 &&
            strcmp(arguments[3], "-Wp,-MMD,/tmp/replay.d") == 0 &&
            strcmp(arguments[4], "-c") == 0 && strcmp(arguments[5], "source.c") == 0 && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_result_controls(char const* temporary)
{
    char path[KERNEL_PATH_BYTES], missing[KERNEL_PATH_BYTES];
    KernelActionResult result;
    bool valid = kernel_path(path, temporary, "complete.result") &&
                 kernel_result_write(path, 0, "trusted-target", "/usr/bin/gcc",
                                     "/tmp/kernel.o", "stat:1:2:3") &&
                 kernel_result_read(path, &result) && result.status == 0 &&
                 strcmp(result.role, "trusted-target") == 0 &&
                 strcmp(result.tool_identity, "resolved-path-only") == 0;
    valid = kernel_path(path, temporary, "invalid-identity.result") &&
            kernel_result_write(path, 0, "trusted-target", "/usr/bin/gcc",
                                "/tmp/kernel.o", "stat:1:bad:3") &&
            !kernel_result_read(path, &result) && valid;
    valid = kernel_path(path, temporary, "truncated.result") &&
            kernel_write(path, "exit=0\nrole=trusted-target\n", 27) &&
            !kernel_result_read(path, &result) && valid;
    valid = kernel_path(missing, temporary, "absent.result") &&
            !kernel_result_read(missing, &result) && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_config_controls(char const* temporary)
{
    char path[KERNEL_PATH_BYTES], status[16];
    bool contradictory = false;
    char const* initial = "# CONFIG_JUMP_LABEL is not set\nCONFIG_HAVE_STATIC_CALL=y\n"
                          "CONFIG_JUMP_LABEL_EXTRA=y\nCONFIG_X86_64=y\n";
    bool valid = kernel_path(path, temporary, "config.fixture") &&
                 kernel_write(path, initial, strlen(initial));
    bool found_jump = valid && kernel_config_symbol(path, "CONFIG_JUMP_LABEL", status,
                                                    sizeof(status), &contradictory);
    valid = valid && found_jump && strcmp(status, "disabled") == 0 && !contradictory;
    bool found_static = kernel_config_symbol(path, "CONFIG_HAVE_STATIC_CALL", status,
                                             sizeof(status), &contradictory);
    valid = found_static && strcmp(status, "enabled") == 0 && !contradictory && valid;
    bool found_missing = kernel_config_symbol(path, "CONFIG_JUMP_LABEL_MISSING", status,
                                              sizeof(status), &contradictory);
    valid = !found_missing && strcmp(status, "unknown") == 0 && !contradictory && valid;
    valid = kernel_config_not_enabled(path, "CONFIG_RUST") && valid;
    char const* rust_enabled = "CONFIG_RUST=y\n";
    valid = kernel_write(path, rust_enabled, strlen(rust_enabled)) && valid;
    valid = !kernel_config_not_enabled(path, "CONFIG_RUST") && valid;
    char const* conflict = "# CONFIG_JUMP_LABEL is not set\nCONFIG_HAVE_STATIC_CALL=y\n"
                           "CONFIG_HAVE_STATIC_CALL=n\nCONFIG_JUMP_LABEL_EXTRA=y\nCONFIG_X86_64=y\n";
    valid = kernel_write(path, conflict, strlen(conflict)) && valid;
    bool found_conflict = kernel_config_symbol(path, "CONFIG_HAVE_STATIC_CALL", status,
                                               sizeof(status), &contradictory);
    valid = found_conflict && contradictory && strcmp(status, "unknown") == 0 && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_mechanism_controls(char const* temporary)
{
    char reference[KERNEL_PATH_BYTES], candidate[KERNEL_PATH_BYTES];
    char config[KERNEL_PATH_BYTES], mechanisms[KERNEL_PATH_BYTES];
    bool valid = kernel_path(reference, temporary, "mechanism-reference") &&
                 kernel_path(candidate, temporary, "mechanism-candidate") &&
                 mkdir(reference, 0700) == 0 && mkdir(candidate, 0700) == 0;
    if (valid)
    {
        strcpy(kernel_context.reference, reference);
        strcpy(kernel_context.candidate, candidate);
        valid = kernel_path(config, reference, ".config") &&
                kernel_write(config,
                             "CONFIG_X86_64=y\nCONFIG_HAVE_OBJTOOL=y\nCONFIG_OBJTOOL=y\n"
                             "CONFIG_MITIGATION_RETPOLINE=y\nCONFIG_MITIGATION_RETHUNK=y\n"
                             "CONFIG_MITIGATION_SLS=y\n",
                             sizeof("CONFIG_X86_64=y\nCONFIG_HAVE_OBJTOOL=y\nCONFIG_OBJTOOL=y\n"
                                    "CONFIG_MITIGATION_RETPOLINE=y\nCONFIG_MITIGATION_RETHUNK=y\n"
                                    "CONFIG_MITIGATION_SLS=y\n") - 1);
    }
    valid = valid && kernel_mechanism_census() &&
            kernel_path(mechanisms, kernel_context.output, "mechanisms.tsv") &&
            kernel_contains(mechanisms, "trusted-reference\tCONFIG_OBJTOOL\tenabled\t") &&
            kernel_contains(mechanisms, "trusted-reference\tCONFIG_MITIGATION_RETPOLINE\tenabled\t") &&
            kernel_contains(mechanisms, "buster-candidate\tCONFIG_OBJTOOL\tunknown\t") &&
            kernel_contains(mechanisms, "buster-candidate\tCONFIG_MITIGATION_SLS\tunknown\t");
    return valid;
}

BUSTER_GLOBAL_LOCAL bool kernel_census_controls(char const* temporary)
{
    char records[KERNEL_PATH_BYTES], record[KERNEL_PATH_BYTES], result[KERNEL_PATH_BYTES];
    char summary[KERNEL_PATH_BYTES], result_path[KERNEL_PATH_BYTES], expected_output[3][KERNEL_PATH_BYTES];
    char* reference_argv[] = {"gcc", "-c", "-o", "reference.o", "reference.c", 0};
    char* host_argv[] = {"gcc", "-c", "-o", "host.o", "host.S", 0};
    char* candidate_argv[] = {"ide", "cc", "-c", "-o", "candidate.o", "candidate.s", 0};
    bool valid = kernel_path(records, temporary, "commands-control") &&
                 mkdir(records, 0700) == 0 &&
                 kernel_path(summary, temporary, "action-census.txt");
    char const* names[] = {"target-fixture.argv", "host-fixture.argv", "buster-fixture.argv"};
    char** commands[] = {reference_argv, host_argv, candidate_argv};
    char const* roles[] = {"trusted-target", "trusted-host", "buster-target"};
    char const* outputs[] = {"reference.o", "host.o", "candidate.o"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && valid; ++i)
    {
        valid = kernel_path(record, records, names[i]) &&
                kernel_argv(record, temporary, commands[i]) &&
                kernel_record_output(temporary, outputs[i], expected_output[i],
                                     sizeof(expected_output[i]));
        size_t length = strlen(names[i]);
        valid = valid && length < sizeof(result);
        if (valid)
        {
            strcpy(result, names[i]);
            strcpy(result + length - 5, ".result");
            valid = kernel_path(result_path, records, result) &&
                    kernel_result_write(result_path, 0, roles[i], "/usr/bin/test-tool",
                                        expected_output[i], "stat:1:2:3");
        }
    }
    strcpy(kernel_context.records, records);
    valid = valid && kernel_action_census() &&
            kernel_contains(summary, "wrapped_action_records=3\n") &&
            kernel_contains(summary, "trusted_reference_wrapped_c_actions=1\n") &&
            kernel_contains(summary, "buster_candidate_wrapped_assembly_inputs=1\n") &&
            kernel_contains(summary, "trusted_host_wrapped_assembly_inputs_excluded=1\n") &&
            kernel_contains(summary, "successful_wrapper_results=3\n") &&
            kernel_contains(summary, "classified_c_object_actions=1\n") &&
            kernel_contains(summary, "replayed_reference_c_actions=recorded-separately-in-target-c.tsv\n");
    valid = kernel_path(result_path, records, "buster-fixture.result") &&
            kernel_result_write(result_path, 0, "buster-target", "/usr/bin/test-tool",
                                "/tmp/unrelated.o", "stat:1:2:3") && valid;
    bool mismatched_output_rejected = !kernel_action_census() &&
                                     kernel_contains(summary, "missing_or_malformed_results=1\n");
    valid = mismatched_output_rejected && valid;
    valid = kernel_result_write(result_path, 0, "buster-target", "/usr/bin/test-tool",
                                expected_output[2], "stat:1:bad:3") && valid;
    bool invalid_identity_rejected = !kernel_action_census() &&
                                     kernel_contains(summary, "missing_or_malformed_results=1\n");
    valid = invalid_identity_rejected && valid;
    valid = kernel_result_write(result_path, 0, "buster-target", "/usr/bin/test-tool",
                                expected_output[2], "stat:1:2:3") && valid;
    valid = kernel_path(result_path, records, "host-fixture.result") &&
            unlink(result_path) == 0 && valid;
    bool missing_result_rejected = !kernel_action_census() &&
                                   kernel_contains(summary, "missing_or_malformed_results=1\n");
    valid = missing_result_rejected && valid;
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
            valid = valid && kernel_classifier_controls(temporary) &&
                    kernel_replay_controls() && kernel_result_controls(temporary) && kernel_config_controls(temporary) &&
                    kernel_mechanism_controls(temporary) && kernel_census_controls(temporary) &&
                    kernel_source_controls(temporary);
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
