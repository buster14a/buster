/* Test-only Linux executor: bounded compiler invocations, independent host
 * observer, and lossless command/status/output retention. It neither builds
 * Buster nor replaces build.c orchestration. Run on a hosted executor only.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PATH_CAPACITY 8192
#define ARGUMENT_CAPACITY 40
static const char* output_directory;
static const char* source;
static FILE* summary;
static unsigned reference_failures, behavioral_failures, setup_failures, repeat_failures;

static void path(char* destination, const char* tag, const char* suffix)
{
    int count = snprintf(destination, PATH_CAPACITY, "%s/%s%s", output_directory, tag, suffix);
    if (count < 0 || count >= PATH_CAPACITY) { fputs("path overflow\n", stderr); exit(2); }
}
static int run(const char* tag, const char* const* arguments)
{
    char file[PATH_CAPACITY];
    path(file, tag, ".argv");
    FILE* record = fopen(file, "wb");
    int result = 125;
    if (record)
    {
        char* actual[ARGUMENT_CAPACITY] = {"timeout", "--kill-after=5s", "120s"};
        unsigned count = 3;
        for (unsigned index = 0; arguments[index]; index += 1)
        {
            if (count + 1 >= ARGUMENT_CAPACITY) { fputs("argument overflow\n", stderr); exit(2); }
            actual[count++] = (char*)arguments[index];
        }
        actual[count] = NULL;
        for (unsigned index = 0; index < count; index += 1) fprintf(record, "%zu:%s\n", strlen(actual[index]), actual[index]);
        fclose(record);
        fflush(NULL);
        pid_t child = fork();
        if (child == 0)
        {
            path(file, tag, ".stdout");
            int out = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            path(file, tag, ".stderr");
            int error = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (out < 0 || error < 0 || dup2(out, STDOUT_FILENO) < 0 || dup2(error, STDERR_FILENO) < 0) _exit(125);
            close(out); close(error);
            execvp(actual[0], actual);
            _exit(127);
        }
        else if (child > 0)
        {
            int status = 0;
            pid_t waited;
            do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
            if (waited == child) result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
    }
    path(file, tag, ".status");
    record = fopen(file, "wb");
    if (record) { fprintf(record, "%d\n", result); fclose(record); }
    printf("command=%s status=%d\n", tag, result);
    return result;
}
static int compare(const char* first, const char* second)
{
    FILE* a = fopen(first, "rb");
    FILE* b = fopen(second, "rb");
    int result = a && b ? 0 : 2;
    bool finished = result != 0;
    while (!finished)
    {
        unsigned char left[4096], right[4096];
        size_t x = fread(left, 1, sizeof(left), a), y = fread(right, 1, sizeof(right), b);
        if (ferror(a) || ferror(b)) result = 2;
        else if (x != y || memcmp(left, right, x)) result = 1;
        finished = result != 0 || x < sizeof(left);
    }
    if (a) fclose(a);
    if (b) fclose(b);
    return result;
}
static void experiment(const char* compiler, const char* tag, bool buster, const char* mode, const char* optimization, const char* observer)
{
    char object[PATH_CAPACITY], executable[PATH_CAPACITY], trace[PATH_CAPACITY], command[128];
    path(object, tag, ".o"); path(executable, tag, ".exe");
    char trace_path[PATH_CAPACITY]; path(trace_path, tag, "");
    int length = snprintf(trace, sizeof(trace), "-fbootstrap-trace=%s", trace_path);
    if (length < 0 || (size_t)length >= sizeof(trace)) { fputs("trace path overflow\n", stderr); exit(2); }
    const char* arguments[ARGUMENT_CAPACITY];
    unsigned count = 0;
    arguments[count++] = compiler;
    if (buster) arguments[count++] = "cc";
    arguments[count++] = "-std=gnu17";
    arguments[count++] = "-fwrapv";
    arguments[count++] = "-fno-strict-aliasing";
    arguments[count++] = "-funsigned-char";
    arguments[count++] = "-g0";
    arguments[count++] = optimization;
    arguments[count++] = "-DSUBJECT_ONLY=1";
    if (buster)
    {
        arguments[count++] = "-target"; arguments[count++] = "x86_64-linux";
        arguments[count++] = mode;
        arguments[count++] = "-fverify-codegen";
        arguments[count++] = "-ffrontend-ssa";
        if (strcmp(mode, "-fregister-allocator=none")) arguments[count++] = "-fno-machine-fallback";
        arguments[count++] = trace;
    }
    arguments[count++] = "-c"; arguments[count++] = source;
    arguments[count++] = "-o"; arguments[count++] = object;
    arguments[count] = NULL;
    snprintf(command, sizeof(command), "%s-compile", tag);
    int compiled = run(command, arguments), linked = -1, executed = -1;
    if (compiled == 0)
    {
        const char* link[] = {"clang", "-no-pie", observer, object, "-lm", "-o", executable, NULL};
        snprintf(command, sizeof(command), "%s-link", tag); linked = run(command, link);
        if (linked == 0)
        {
            const char* execute[] = {executable, NULL};
            snprintf(command, sizeof(command), "%s-run", tag); executed = run(command, execute);
        }
    }
    fprintf(summary, "%s\t%d\t%d\t%d\n", tag, compiled, linked, executed); fflush(summary);
    if (!buster) reference_failures += compiled != 0 || linked != 0 || executed != 0;
    else
    {
        behavioral_failures += compiled == 0 && linked == 0 && executed == 1;
        setup_failures += compiled != 0 || linked != 0 || (executed != 0 && executed != 1);
    }
}
int main(int argc, char** argv)
{
    int result = 2;
    if (argc != 6) fputs("usage: observe source.c fresh-output ide-C0 ide-C1 ide-C2\n", stderr);
    else
    {
        source = argv[1]; output_directory = argv[2];
        if (mkdir(output_directory, 0700) != 0) { perror("fresh output mkdir"); exit(2); }
        char name[PATH_CAPACITY], observer[PATH_CAPACITY];
        path(name, "summary", ".tsv"); summary = fopen(name, "wb");
        if (!summary) { perror("summary"); exit(2); }
        fputs("cell\tcompile\tlink\texecute\n", summary);
        path(observer, "independent-observer", ".o");
        const char* build_observer[] = {"clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-O2", "-c", source, "-o", observer, NULL};
        int observer_status = run("build-observer", build_observer);
        if (observer_status == 0)
        {
            const char* compilers[] = {"clang", "gcc"};
            const char* optimizations[] = {"-O0", "-O2"};
            for (unsigned producer = 0; producer < 2; producer += 1)
                for (unsigned optimization = 0; optimization < 2; optimization += 1)
                {
                    char tag[64]; snprintf(tag, sizeof(tag), "reference-%s-O%u", compilers[producer], optimization * 2);
                    experiment(compilers[producer], tag, false, "", optimizations[optimization], observer);
                }
            const char* modes[] = {"-fregister-allocator=fast", "-fregister-allocator=none"};
            const char* mode_names[] = {"fast", "none"};
            for (unsigned generation = 0; generation < 3; generation += 1)
                for (unsigned mode = 0; mode < 2; mode += 1)
                {
                    for (unsigned repeat = 0; repeat < 2; repeat += 1)
                    {
                        char tag[64]; snprintf(tag, sizeof(tag), "C%u-%s-r%u", generation, mode_names[mode], repeat);
                        experiment(argv[generation + 3], tag, true, modes[mode], "-O0", observer);
                    }
                    const char* suffixes[] = {".o", "-run.stdout", "-run.stderr", "-run.status"};
                    for (unsigned item = 0; item < 4; item += 1)
                    {
                        char first[PATH_CAPACITY], second[PATH_CAPACITY], tag[64];
                        snprintf(tag, sizeof(tag), "C%u-%s-r0", generation, mode_names[mode]); path(first, tag, suffixes[item]);
                        snprintf(tag, sizeof(tag), "C%u-%s-r1", generation, mode_names[mode]); path(second, tag, suffixes[item]);
                        int difference = compare(first, second);
                        printf("repeat=C%u-%s suffix=%s difference=%d\n", generation, mode_names[mode], suffixes[item], difference);
                        repeat_failures += difference != 0;
                    }
                }
            printf("reference_failures=%u behavioral_failures=%u setup_failures=%u repeat_failures=%u\n", reference_failures, behavioral_failures, setup_failures, repeat_failures);
            result = reference_failures || setup_failures || repeat_failures ? 2 : behavioral_failures != 0;
        }
        fclose(summary);
    }
    return result;
}
