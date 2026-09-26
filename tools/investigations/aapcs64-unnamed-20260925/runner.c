/* Standalone, bounded hosted correctness orchestration. No performance claims.
 * Every command, exit code, object and observer output is retained.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/wait.h>

static FILE* records;
static unsigned setup_errors;
static unsigned observations;

static int execute(char const* name, char const* output, char const* format, ...)
{
    char command[8192];
    char redirected[9216];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(command, sizeof(command), format, args);
    va_end(args);
    int result = 125;
    if (length >= 0 && (size_t)length < sizeof(command))
    {
        length = snprintf(redirected, sizeof(redirected), "timeout 120s %s > '%s' 2>&1", command, output);
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
    fprintf(records, "%s\t%d\t%s\n", name, result, command);
    fflush(records);
    printf("%s exit=%d\n", name, result);
    fflush(stdout);
    return result;
}

int main(int argc, char** argv)
{
    int result = 2;
    if (argc == 5)
    {
        char const* ide = argv[1];
        char const* source = argv[2];
        char const* out = argv[3];
        char const* target = argv[4];
        char path[1024];
        snprintf(path, sizeof(path), "%s/commands.tsv", out);
        records = fopen(path, "w");
        if (records)
        {
            char const* flags = "-std=c17 -fwrapv -fno-strict-aliasing -funsigned-char";
            char const* references[] = {"clang", "gcc"};
            char const* modes[] = {"none", "mir-stack", "fast", "quality"};
            char observer[2][1024] = {{0}};
            fprintf(records, "label\texit\tcommand\n");
            for (unsigned ref = 0; ref < 2; ref += 1)
            {
                snprintf(observer[ref], sizeof(observer[ref]), "%s/observer-%s.o", out, references[ref]);
                snprintf(path, sizeof(path), "%s/observer-%s.log", out, references[ref]);
                setup_errors += execute("observer", path, "%s %s -O2 -c '%s/witness.c' -o '%s'", references[ref], flags, source, observer[ref]) != 0;
                for (unsigned opt = 0; opt < 2; opt += 1)
                {
                    char object[1024];
                    char binary[1024];
                    snprintf(object, sizeof(object), "%s/reference-%s-O%u.o", out, references[ref], opt*2);
                    snprintf(binary, sizeof(binary), "%s/reference-%s-O%u", out, references[ref], opt*2);
                    snprintf(path, sizeof(path), "%s/reference-%s-O%u-compile.log", out, references[ref], opt*2);
                    int compiled = execute("reference-compile", path, "%s %s -O%u -DSUBJECT -c '%s/witness.c' -o '%s'", references[ref], flags, opt*2, source, object);
                    setup_errors += compiled != 0;
                    if (!compiled)
                    {
                        snprintf(path, sizeof(path), "%s/reference-%s-O%u-link.log", out, references[ref], opt*2);
                        int linked = execute("reference-link", path, "%s '%s' '%s' -o '%s'", references[ref], observer[ref], object, binary);
                        setup_errors += linked != 0;
                        if (!linked)
                        {
                            snprintf(path, sizeof(path), "%s/reference-%s-O%u-run.log", out, references[ref], opt*2);
                            setup_errors += execute("reference-run", path, "'%s'", binary) != 0;
                            observations += 1;
                        }
                    }
                }
            }
            for (unsigned mode = 0; mode < 4; mode += 1)
            {
                for (unsigned front = 0; front < 2; front += 1)
                {
                    char object[1024];
                    char binary[1024];
                    snprintf(object, sizeof(object), "%s/buster-%s-%u.o", out, modes[mode], front);
                    snprintf(path, sizeof(path), "%s/buster-%s-%u-compile.log", out, modes[mode], front);
                    int compiled = execute("buster-compile", path, "'%s' cc %s -target %s -O0 -g0 -DSUBJECT -fverify-codegen -fregister-allocator=%s %s %s -c '%s/witness.c' -o '%s'", ide, flags, target, modes[mode], front ? "-fno-frontend-ssa" : "-ffrontend-ssa", mode ? "-fno-machine-fallback" : "", source, object);
                    setup_errors += compiled != 0;
                    if (!compiled)
                    {
                        snprintf(path, sizeof(path), "%s/buster-%s-%u-symbols.log", out, modes[mode], front);
                        setup_errors += execute("symbols", path, "readelf -Ws '%s'", object) != 0;
                        for (unsigned ref = 0; ref < 2; ref += 1)
                        {
                            snprintf(binary, sizeof(binary), "%s/buster-%s-%u-%s", out, modes[mode], front, references[ref]);
                            snprintf(path, sizeof(path), "%s/buster-%s-%u-%s-link.log", out, modes[mode], front, references[ref]);
                            int linked = execute("buster-link", path, "%s '%s' '%s' -o '%s'", references[ref], observer[ref], object, binary);
                            setup_errors += linked != 0;
                            if (!linked)
                            {
                                snprintf(path, sizeof(path), "%s/buster-%s-%u-%s-run.log", out, modes[mode], front, references[ref]);
                                int observed = execute("buster-observe", path, "'%s'", binary);
                                setup_errors += observed != 0 && observed != 1;
                                observations += 1;
                            }
                        }
                    }
                }
            }
            snprintf(path, sizeof(path), "%s/canonical-compile.log", out);
            setup_errors += execute("canonical", path, "'%s' cc %s -target %s -O0 -g0 -DSUBJECT -emit-llvm -c '%s/witness.c' -o '%s/canonical.bc'", ide, flags, target, source, out) != 0;
            snprintf(path, sizeof(path), "%s/reference-llvm.log", out);
            setup_errors += execute("reference-llvm", path, "clang %s -O0 -DSUBJECT -S -emit-llvm '%s/witness.c' -o '%s/reference.ll'", flags, source, out) != 0;
            snprintf(path, sizeof(path), "%s/canonical-decode.log", out);
            execute("llvm-dis", path, "llvm-dis-21 '%s/canonical.bc' -o '%s/canonical.ll'", out, out);
            printf("AUDIT observations=%u setup_errors=%u; observer exit 1 is a recorded semantic discrepancy, not success.\n", observations, setup_errors);
            fclose(records);
            result = setup_errors != 0;
        }
    }
    return result;
}
