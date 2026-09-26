/* Bounded research-only hosted process orchestration, not a performance tool.
 * All commands, statuses and outputs survive semantic failures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/wait.h>

static FILE* records;
static unsigned setup_errors;
static unsigned observations;
static unsigned semantic_failures;
static unsigned negative_controls;
static char const* output_root;

typedef struct Compiler
{
    char name[64];
    char invocation[2048];
    char caller[1024];
    char callee[1024];
    int ready;
} Compiler;

static int execute(char const* label, char const* output, char const* format, ...)
{
    char command[8192] = {0};
    char redirected[10240];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(command, sizeof(command), format, args);
    va_end(args);
    int result = 125;
    if (length >= 0 && (size_t)length < sizeof(command))
    {
        length = snprintf(redirected, sizeof(redirected), "timeout 45s %s > '%s' 2>&1", command, output);
        if (length >= 0 && (size_t)length < sizeof(redirected))
        {
            int status = system(redirected);
            if (status != -1 && WIFEXITED(status))
            {
                result = WEXITSTATUS(status);
            }
            else if (status != -1 && WIFSIGNALED(status))
            {
                result = 128 + WTERMSIG(status);
            }
        }
    }
    fprintf(records, "%s\t%d\t%s\n", label, result, command);
    fflush(records);
    printf("%s exit=%d\n", label, result);
    fflush(stdout);
    return result;
}

static void build_pair(Compiler* compiler, char const* source)
{
    char const* roles[] = {"caller", "callee"};
    compiler->ready = 1;
    for (unsigned role = 0; role < 2; role += 1)
    {
        char* object = role ? compiler->callee : compiler->caller;
        char log[1024];
        snprintf(object, 1024, "%s/%s-%s.o", output_root, compiler->name, roles[role]);
        snprintf(log, sizeof(log), "%s/%s-%s-compile.log", output_root, compiler->name, roles[role]);
        int status = execute("compile", log, "%s -c '%s/%s.c' -o '%s'", compiler->invocation, source, roles[role], object);
        setup_errors += status != 0;
        compiler->ready &= status == 0;
        if (!status)
        {
            snprintf(log, sizeof(log), "%s/%s-%s-disassembly.log", output_root, compiler->name, roles[role]);
            setup_errors += execute("disassembly-evidence-only", log, "objdump -dr '%s'", object) != 0;
        }
    }
}

static void observe_pair(Compiler const* caller, Compiler const* callee,
                         char const* observer, char const* observer_name,
                         int reference, int negative)
{
    if (caller->ready && callee->ready)
    {
        char label[256];
        char binary[1024];
        char log[1024];
        snprintf(label, sizeof(label), "%s-to-%s-%s%s", caller->name, callee->name, observer_name, negative ? "-negative" : "");
        snprintf(binary, sizeof(binary), "%s/%s", output_root, label);
        snprintf(log, sizeof(log), "%s/%s-link.log", output_root, label);
        int linked = execute("link", log, "clang -no-pie '%s' '%s' '%s' -o '%s'", caller->caller, callee->callee, observer, binary);
        setup_errors += linked != 0;
        if (!linked)
        {
            snprintf(log, sizeof(log), "%s/%s-run.log", output_root, label);
            int status = execute(label, log, "'%s'", binary);
            FILE* stream = fopen(log, "r");
            int opened = stream != NULL;
            unsigned markers = 0;
            unsigned mismatches = 0;
            unsigned rows = 0;
            char line[1024];
            if (stream)
            {
                while (fgets(line, sizeof(line), stream))
                {
                    if (strncmp(line, "row=", 4) == 0)
                    {
                        rows += 1;
                    }
                    if (sscanf(line, "SUMMARY rows=5 cells=15 mismatches=%u", &mismatches) == 1)
                    {
                        markers += 1;
                    }
                }
                fclose(stream);
            }
            int complete = opened && rows == 5 && markers == 1 && mismatches <= 15
                         && (status == 0 || status == 1) && status == (mismatches != 0);
            setup_errors += !complete;
            if (complete)
            {
                if (negative)
                {
                    negative_controls += 1;
                    setup_errors += status != 1 || mismatches != 1;
                }
                else
                {
                    observations += 1;
                    semantic_failures += status != 0;
                    setup_errors += reference && status != 0;
                }
            }
        }
    }
}

int main(int argc, char** argv)
{
    int result = 2;
    int arguments_valid = argc == 5;
    for (int arg = 1; arg < argc; arg += 1)
    {
        arguments_valid &= strspn(argv[arg], "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-.") == strlen(argv[arg]);
    }
    if (arguments_valid)
    {
        char const* ide = argv[1];
        char const* source = argv[2];
        output_root = argv[3];
        char const* target = argv[4];
        char path[1024];
        snprintf(path, sizeof(path), "%s/commands.tsv", output_root);
        records = fopen(path, "w");
        if (records)
        {
            char const* common = "-std=c17 -g0 -fwrapv -fno-strict-aliasing -funsigned-char";
            char const* references[] = {"clang", "gcc"};
            char const* modes[] = {"none", "mir-stack", "fast", "quality"};
            Compiler compilers[12] = {0};
            char observers[2][1024] = {{0}};
            char negatives[2][1024] = {{0}};
            int observer_ready[2] = {0};
            int negative_ready[2] = {0};
            fprintf(records, "label\texit\tcommand\n");
            for (unsigned ref = 0; ref < 2; ref += 1)
            {
                snprintf(observers[ref], sizeof(observers[ref]), "%s/observer-%s.o", output_root, references[ref]);
                snprintf(negatives[ref], sizeof(negatives[ref]), "%s/observer-%s-negative.o", output_root, references[ref]);
                snprintf(path, sizeof(path), "%s/observer-%s.log", output_root, references[ref]);
                observer_ready[ref] = execute("observer", path, "%s %s -O2 -Wall -Wextra -Werror -pedantic-errors -c '%s/observer.c' -o '%s'", references[ref], common, source, observers[ref]) == 0;
                snprintf(path, sizeof(path), "%s/observer-%s-negative.log", output_root, references[ref]);
                negative_ready[ref] = execute("negative-observer", path, "%s %s -O2 -Wall -Wextra -Werror -pedantic-errors -DWRONG_EXPECTATION=1 -c '%s/observer.c' -o '%s'", references[ref], common, source, negatives[ref]) == 0;
                setup_errors += !observer_ready[ref] || !negative_ready[ref];
                for (unsigned opt = 0; opt < 2; opt += 1)
                {
                    Compiler* compiler = &compilers[ref * 2 + opt];
                    snprintf(compiler->name, sizeof(compiler->name), "%s-O%u", references[ref], opt * 2);
                    snprintf(compiler->invocation, sizeof(compiler->invocation), "%s %s -O%u -Wall -Wextra -Werror -pedantic-errors -fno-lto", references[ref], common, opt * 2);
                    build_pair(compiler, source);
                }
            }
            for (unsigned mode = 0; mode < 4; mode += 1)
            {
                for (unsigned front = 0; front < 2; front += 1)
                {
                    Compiler* compiler = &compilers[4 + mode * 2 + front];
                    snprintf(compiler->name, sizeof(compiler->name), "buster-%s-%s", modes[mode], front ? "memory" : "ssa");
                    snprintf(compiler->invocation, sizeof(compiler->invocation), "'%s' cc %s -O0 -nostdinc -target %s -fverify-codegen -fregister-allocator=%s %s %s", ide, common, target, modes[mode], front ? "-fno-frontend-ssa" : "-ffrontend-ssa", mode ? "-fno-machine-fallback" : "");
                    build_pair(compiler, source);
                }
            }
            for (unsigned obs = 0; obs < 2; obs += 1)
            {
                if (observer_ready[obs])
                {
                    for (unsigned caller = 0; caller < 4; caller += 1)
                    {
                        for (unsigned callee = 0; callee < 4; callee += 1)
                        {
                            observe_pair(&compilers[caller], &compilers[callee], observers[obs], references[obs], 1, 0);
                        }
                    }
                    for (unsigned buster = 4; buster < 12; buster += 1)
                    {
                        observe_pair(&compilers[buster], &compilers[buster], observers[obs], references[obs], 0, 0);
                        for (unsigned ref = 0; ref < 4; ref += 1)
                        {
                            observe_pair(&compilers[buster], &compilers[ref], observers[obs], references[obs], 0, 0);
                            observe_pair(&compilers[ref], &compilers[buster], observers[obs], references[obs], 0, 0);
                        }
                    }
                }
                if (negative_ready[obs])
                {
                    observe_pair(&compilers[0], &compilers[0], negatives[obs], references[obs], 0, 1);
                }
            }
            setup_errors += observations != 176 || negative_controls != 2;
            printf("COLLECTION observations=%u semantic_failures=%u negative_controls=%u setup_errors=%u\n", observations, semantic_failures, negative_controls, setup_errors);
            printf("Collector success is not semantic acceptance; every nonzero observer result is retained.\n");
            fclose(records);
            result = setup_errors != 0;
        }
    }
    return result;
}
