#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int write_text(char const* path, char const* text)
{
    FILE* file = fopen(path, "wb");
    int okay = file && fwrite(text, 1, strlen(text), file) == strlen(text);
    if (file && fclose(file) != 0) { okay = 0; }
    return okay;
}

static size_t read_text(char const* path, char* text, size_t capacity)
{
    FILE* file = fopen(path, "rb");
    size_t count = file ? fread(text, 1, capacity - 1, file) : 0;
    text[count] = 0;
    if (file) { fclose(file); }
    return count;
}

static int run(char* const* argv, char const* log)
{
    int status = -1;
    pid_t child = fork();
    if (child == 0)
    {
        int fd = open(log, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) { _exit(125); }
        close(fd);
        execv(argv[0], argv);
        _exit(126);
    }
    if (child > 0)
    {
        int waited = 0;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        status = waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return status;
}

int main(int argc, char** argv)
{
    int result = 0;
    char const* const sentinel = "preserve existing output";
    char const* const targets[] = {"aarch64-apple-macos", "aarch64-apple-ios"};
    char const* const options[] = {"-l", "-framework"};
    if (argc != 3 || (mkdir("proof-3643", 0700) != 0 && errno != EEXIST))
    {
        result = 1;
    }
    char cwd[4096];
    char root[4096];
    char source[4096];
    char other[4096];
    char output[4096];
    if (!result)
    {
        if (!getcwd(cwd, sizeof(cwd)) ||
            snprintf(root, sizeof(root), "%s/proof-3643", cwd) >= (int)sizeof(root) ||
            snprintf(source, sizeof(source), "%s/main.c", root) >= (int)sizeof(source) ||
            snprintf(other, sizeof(other), "%s/other.c", root) >= (int)sizeof(other) ||
            snprintf(output, sizeof(output), "%s/existing", root) >= (int)sizeof(output) ||
            !write_text(source, "int main(void) { return 0; }\n") ||
            !write_text(other, "int other(void) { return 1; }\n"))
        {
            result = 1;
        }
    }
    unsigned observed = 0;
    for (unsigned compiler = 0; !result && compiler < 2; compiler += 1)
    {
        for (unsigned target = 0; target < 2; target += 1)
        {
            for (unsigned option = 0; option < 2; option += 1)
            {
                for (unsigned multi = 0; multi < 2; multi += 1)
                {
                    for (unsigned following = 0; following < 2; following += 1)
                    {
                        char log[4096];
                        snprintf(log, sizeof(log), "proof-3643/c%u-t%u-o%u-m%u-f%u.log", compiler, target, option, multi, following);
                        char* command[20];
                        unsigned count = 0;
                        command[count++] = argv[1 + compiler];
                        command[count++] = "cc";
                        command[count++] = "-target";
                        command[count++] = (char*)targets[target];
                        command[count++] = "--sysroot";
                        command[count++] = root;
                        command[count++] = source;
                        if (multi) { command[count++] = other; }
                        command[count++] = (char*)options[option];
                        command[count++] = "";
                        if (following)
                        {
                            command[count++] = (char*)options[option];
                            command[count++] = "BusterMissing3643";
                        }
                        command[count++] = "-o";
                        command[count++] = output;
                        command[count] = 0;
                        int written = write_text(output, sentinel);
                        int status = written ? run(command, log) : -1;
                        char diagnostic[8192];
                        char image[256];
                        read_text(log, diagnostic, sizeof(diagnostic));
                        size_t bytes = read_text(output, image, sizeof(image));
                        int preserved = bytes == strlen(sentinel) && memcmp(image, sentinel, bytes) == 0;
                        int argument_error = strstr(diagnostic, "empty dependency name after ") != 0;
                        int okay = compiler ? status == 1 && preserved && argument_error
                                            : following ? status == 1 && preserved && !argument_error &&
                                                          strstr(diagnostic, "BusterMissing3643") != 0
                                                        : status == 0 && !preserved && !argument_error;
                        printf("EMPTY_DEPENDENCY_PROOF compiler=%s target=%s option=%s multi=%u following=%u status=%d preserved=%d argument_error=%d verdict=%s\n%s",
                               compiler ? "candidate" : "baseline", targets[target], options[option], multi, following,
                               status, preserved, argument_error, okay ? "pass" : "fail", diagnostic);
                        observed += (unsigned)okay;
                        result |= !okay;
                    }
                }
            }
        }
    }
    printf("EMPTY_DEPENDENCY_PROOF_SUMMARY expected=32 observed=%u status=%s\n", observed, !result && observed == 32 ? "pass" : "fail");
    result |= observed != 32;
    return result;
}
