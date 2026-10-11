#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int run(char* const* argv, char const* log)
{
    int status = -1;
    pid_t child = fork();
    if (!child)
    {
        int fd = open(log, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (fd < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) { _exit(125); }
        close(fd);
        execvp(argv[0], argv);
        _exit(126);
    }
    if (child > 0)
    {
        int waited;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        status = waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return status;
}

static uint64_t instructions(char const* path)
{
    FILE* file = fopen(path, "r");
    uint64_t count = 0;
    char line[4096];
    while (file && fgets(line, sizeof(line), file))
    {
        if (sscanf(line, "summary: %" SCNu64, &count) == 1) { break; }
    }
    if (file) { fclose(file); }
    return count;
}

static int identical(char const* left, char const* right)
{
    FILE* a = fopen(left, "rb");
    FILE* b = fopen(right, "rb");
    int same = a && b;
    unsigned char first[4096], second[4096];
    while (same)
    {
        size_t an = fread(first, 1, sizeof(first), a);
        size_t bn = fread(second, 1, sizeof(second), b);
        same = an == bn && !memcmp(first, second, an);
        if (!an) { break; }
    }
    if (a) { fclose(a); }
    if (b) { fclose(b); }
    return same;
}

int main(int argc, char** argv)
{
    int result = argc != 3 || (mkdir("proof-3497", 0700) != 0 && errno != EEXIST);
    unsigned depths[] = {1000, 2000};
    uint64_t counts[2][2][2] = {{{0}}};
    for (unsigned sample = 0; !result && sample < 2; sample += 1)
    {
        char source[256];
        snprintf(source, sizeof(source), "proof-3497/tag-%u.c", depths[sample]);
        FILE* input = fopen(source, "wb");
        if (!input) { result = 1; break; }
        for (unsigned level = 0; level < depths[sample]; level += 1) { fprintf(input, "struct Tag%u {\n", level); }
        fputs("int value;\n", input);
        for (unsigned level = depths[sample]; level > 1; level -= 1) { fprintf(input, "} member%u;\n", level - 1); }
        fputs("};\n_Static_assert(sizeof(struct Tag0)==sizeof(int), \"layout\");\nint nested_tag_value(void){return sizeof(struct Tag0);}\n", input);
        result |= fclose(input) != 0;
        for (unsigned compiler = 0; compiler < 2; compiler += 1)
        {
            for (unsigned pilot = 0; pilot < 2; pilot += 1)
            {
                char profile[256], profile_option[300], log[256];
                snprintf(profile, sizeof(profile), "proof-3497/callgrind-c%u-p%u-d%u.out", compiler, pilot, depths[sample]);
                snprintf(profile_option, sizeof(profile_option), "--callgrind-out-file=%s", profile);
                snprintf(log, sizeof(log), "proof-3497/syntax-c%u-p%u-d%u.log", compiler, pilot, depths[sample]);
                char* command[20] = {"valgrind", "--tool=callgrind", "--cache-sim=no", "--branch-sim=no", profile_option,
                                     argv[1 + compiler], "cc", "-nostdinc", "-target", "x86_64-unknown-linux", "-march=znver5",
                                     "-fsyntax-only", source, 0};
                if (pilot) { command[13] = "-fc-ast-pilot"; command[14] = 0; }
                int status = run(command, log);
                counts[compiler][pilot][sample] = instructions(profile);
                int okay = status == 0 && counts[compiler][pilot][sample] != 0;
                printf("NESTED_TAG_INSTRUCTIONS compiler=%s pilot=%u depth=%u instructions=%" PRIu64 " status=%d verdict=%s\n",
                       compiler ? "candidate" : "baseline", pilot, depths[sample], counts[compiler][pilot][sample], status, okay ? "pass" : "fail");
                result |= !okay;
                for (unsigned debug = 0; debug < 2; debug += 1)
                {
                    char output[256], object_log[256];
                    snprintf(output, sizeof(output), "proof-3497/c%u-p%u-g%u-d%u.o", compiler, pilot, debug, depths[sample]);
                    snprintf(object_log, sizeof(object_log), "proof-3497/object-c%u-p%u-g%u-d%u.log", compiler, pilot, debug, depths[sample]);
                    char* emit[18] = {argv[1 + compiler], "cc", "-nostdinc", "-target", "x86_64-unknown-linux",
                                      "-march=znver5", "-c", debug ? "-g" : "-g0", "-o", output, source, 0};
                    if (pilot) { emit[11] = "-fc-ast-pilot"; emit[12] = 0; }
                    result |= run(emit, object_log) != 0;
                }
            }
        }
        for (unsigned pilot = 0; pilot < 2; pilot += 1)
        {
            for (unsigned debug = 0; debug < 2; debug += 1)
            {
                char baseline[256], candidate[256];
                snprintf(baseline, sizeof(baseline), "proof-3497/c0-p%u-g%u-d%u.o", pilot, debug, depths[sample]);
                snprintf(candidate, sizeof(candidate), "proof-3497/c1-p%u-g%u-d%u.o", pilot, debug, depths[sample]);
                int same = identical(baseline, candidate);
                printf("NESTED_TAG_OBJECT pilot=%u debug=%u depth=%u identical=%d\n", pilot, debug, depths[sample], same);
                result |= !same;
            }
        }
    }
    for (unsigned compiler = 0; compiler < 2; compiler += 1)
    {
        for (unsigned pilot = 0; pilot < 2; pilot += 1)
        {
            uint64_t shallow = counts[compiler][pilot][0], deep = counts[compiler][pilot][1];
            int okay = shallow && deep && (compiler ? deep * 10 <= shallow * 22 : deep >= shallow * 3);
            printf("NESTED_TAG_GROWTH compiler=%s pilot=%u depth1000=%" PRIu64 " depth2000=%" PRIu64 " ratio=%.6f verdict=%s\n",
                   compiler ? "candidate" : "baseline", pilot, shallow, deep, shallow ? (double)deep / (double)shallow : 0.0, okay ? "pass" : "fail");
            result |= !okay;
        }
    }
    printf("NESTED_TAG_PROOF_SUMMARY status=%s\n", result ? "fail" : "pass");
    return result;
}
