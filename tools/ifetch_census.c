/* Research-only #1438: same-root producer builds; no timing verdict.
   Mutations are exact-anchor checked and restored before any input is read.
   Commands use only fixed strings and the closed variant/case names below. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

static FILE *ledger;
static int errors;
static const char *parse_path = "src/buster/lib/compiler/frontend/c/c_parse.c";
static const char *ide_path = "src/buster/apps/ide/ide.c";
static const char *signature = "BUSTER_C_SHARED void c_parse_diagnostic(CParseResult* result, CSourceLocation location, CDiagnosticKind kind, String8 message)\n{";

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *s = NULL;
    if (f)
    {
        if (!fseek(f, 0, SEEK_END))
        {
            long n = ftell(f);
            if (n >= 0 && n < 16000000 && !fseek(f, 0, SEEK_SET))
            {
                s = malloc((size_t)n + 1);
                if (s && fread(s, 1, (size_t)n, f) == (size_t)n) s[n] = 0;
                else { free(s); s = NULL; }
            }
        }
        fclose(f);
    }
    return s;
}
static int write_all(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");
    int ok = 0;
    if (f)
    {
        ok = fwrite(s, 1, strlen(s), f) == strlen(s);
        if (fclose(f)) ok = 0;
    }
    return ok;
}
static char *replace_one(const char *source, const char *anchor, const char *replacement)
{
    const char *p = strstr(source, anchor);
    char *s = NULL;
    if (p && !strstr(p + strlen(anchor), anchor))
    {
        size_t prefix = (size_t)(p - source);
        size_t n = strlen(source) - strlen(anchor) + strlen(replacement);
        s = malloc(n + 1);
        if (s)
        {
            memcpy(s, source, prefix);
            memcpy(s + prefix, replacement, strlen(replacement));
            strcpy(s + prefix + strlen(replacement), p + strlen(anchor));
        }
    }
    return s;
}
static int run(const char *command)
{
    fprintf(ledger, "$ %s\n", command); fflush(ledger);
    int status = system(command);
    int result = status >= 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 255;
    fprintf(ledger, "exit=%d\n", result); fflush(ledger);
    return result;
}
static int build_variant(const char *variant, const char *parse_original, const char *ide_original)
{
    char *p = NULL, *i = NULL;
    char replacement[1024], command[2048];
    int ok = 1;
    if (strcmp(variant, "A"))
    {
        if (!strcmp(variant, "Q"))
        {
            snprintf(replacement, sizeof replacement, "BUSTER_GLOBAL_LOCAL AtomicU64 ifetch_diagnostic_calls;\n%s\n    atomic_u64_increment(&ifetch_diagnostic_calls);", signature);
            p = replace_one(parse_original, signature, replacement);
            i = replace_one(ide_original,
                "    arena_destroy(arena, 1);\n    return result;\n}\n\n\n#if BUSTER_INCLUDE_TESTS\nBUSTER_GLOBAL_LOCAL ProcessResult compiler_process_spawn_probe",
                "    string_print(S8(\"IFETCH_DIAGNOSTICS calls={u64}\\n\"), atomic_u64_add(&ifetch_diagnostic_calls, 0));\n    arena_destroy(arena, 1);\n    return result;\n}\n\n\n#if BUSTER_INCLUDE_TESTS\nBUSTER_GLOBAL_LOCAL ProcessResult compiler_process_spawn_probe");
            ok = p && i;
        }
        else
        {
            snprintf(replacement, sizeof replacement, "BUSTER_C_SHARED %s __attribute__((noinline)) void c_parse_diagnostic(CParseResult* result, CSourceLocation location, CDiagnosticKind kind, String8 message)\n{", !strcmp(variant,"C") ? "BUSTER_COLD" : "");
            p = replace_one(parse_original, signature, replacement);
            ok = p != NULL;
        }
        if (ok) ok = write_all(parse_path, p) && (!i || write_all(ide_path, i));
    }
    if (ok)
    {
        snprintf(command, sizeof command, "/tmp/ifetch-build build --config Release -t ide > evidence/build-%s.log 2>&1", variant);
        ok = run(command) == 0;
    }
    /* Restore even after a failed build; a compiler never consumes modified inputs. */
    if (!write_all(parse_path, parse_original) || !write_all(ide_path, ide_original)) ok = 0;
    if (ok)
    {
        snprintf(command, sizeof command, "cp build/Release/ide evidence/ide-%s && sha256sum evidence/ide-%s > evidence/binary-%s.sha256 && nm -S --size-sort evidence/ide-%s > evidence/nm-%s.txt && size -A evidence/ide-%s > evidence/sections-%s.txt && objdump -d --no-show-raw-insn evidence/ide-%s > evidence/asm-%s.txt", variant,variant,variant,variant,variant,variant,variant,variant,variant);
        ok = run(command) == 0;
    }
    free(p); free(i);
    return ok;
}
static void cases(const char *variant)
{
    const char *names[] = {"tiny", "basic", "headers", "unity", "semantic", "invalid", "rollback"};
    const char *inputs[] = {"evidence/tiny.c", "tests/basic_c_operations.c", "evidence/headers.c", "src/buster/apps/ide/ide.c", "src/buster/apps/ide/ide.c", "evidence/invalid.c", "evidence/rollback.c"};
    for (size_t c = 0; c < sizeof names / sizeof names[0]; ++c)
    {
        char command[2048];
        snprintf(command, sizeof command,
            "rm -f evidence/output.o evidence/source.metrics; evidence/ide-%s cc -fcompile-jobs=1 -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 %s -fsource-metrics=evidence/source.metrics %s -o evidence/output.o > evidence/%s-%s.stdout 2> evidence/%s-%s.stderr",
            variant, c == 4 ? "-fsyntax-only" : "-g -c", inputs[c],variant,names[c],variant,names[c]);
        int rc = run(command);
        snprintf(command, sizeof command, "evidence/%s-%s.status", variant,names[c]);
        char text[32]; snprintf(text, sizeof text, "%d\n",rc); errors += !write_all(command,text);
        snprintf(command, sizeof command,
            "if test -f evidence/output.o; then cp evidence/output.o evidence/%s-%s.o; fi; if test -f evidence/source.metrics; then cp evidence/source.metrics evidence/%s-%s.metrics; fi",
            variant,names[c],variant,names[c]);
        errors += run(command) != 0;
        if (c < 5 && rc) errors++;
        if (c >= 5 && rc != 1) errors++;
    }
}
static int fixed_point(void)
{
    int ok = run("evidence/ide-A cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o evidence/stage1 > evidence/stage1.stdout 2> evidence/stage1.stderr") == 0;
    if (ok) ok = run("evidence/stage1 cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g src/buster/apps/ide/ide.c -lm -o evidence/stage2 > evidence/stage2.stdout 2> evidence/stage2.stderr") == 0;
    if (ok) ok = run("cmp evidence/stage1 evidence/stage2 && sha256sum evidence/stage1 evidence/stage2 > evidence/fixed-point.sha256") == 0;
    return ok;
}
int main(int argc, char **argv)
{
    int outline = argc == 2 && !strcmp(argv[1], "outline");
    mkdir("evidence",0700);
    ledger = fopen("evidence/commands.txt","w");
    char *p = read_all(parse_path), *i = read_all(ide_path);
    int ok = ledger && p && i && (argc == 1 || outline);
    if (ok)
    {
        ok = write_all("evidence/tiny.c", "int main(void) { return 0; }\n") &&
            write_all("evidence/invalid.c", "unsigned float x;\n") &&
            write_all("evidence/rollback.c", "struct S { int a : ; }; int n = sizeof(struct S);\n") &&
            write_all("evidence/headers.c", "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <limits.h>\n#include <errno.h>\n#include <ctype.h>\n#include <math.h>\n#include <time.h>\n#include <signal.h>\n#include <assert.h>\n#include <float.h>\n#include <locale.h>\n#include <wchar.h>\n#include <wctype.h>\n#include <complex.h>\n#include <fenv.h>\n#include <inttypes.h>\n#include <setjmp.h>\n#include <stdarg.h>\n#include <stdatomic.h>\n#include <sys/types.h>\n#include <sys/stat.h>\n#include <unistd.h>\n#include <fcntl.h>\n#include <dirent.h>\n#include <sys/mman.h>\n#include <pthread.h>\n#include <poll.h>\n#include <semaphore.h>\n#include <sys/uio.h>\n#include <sys/socket.h>\n#include <arpa/inet.h>\nint main(void) { return 0; }\n");
        if (ok) ok = run("clang --version > evidence/producer.txt; uname -a > evidence/kernel.txt; lscpu > evidence/cpu.txt; cat /proc/sys/kernel/perf_event_paranoid > evidence/perf-paranoid.txt; ls /sys/bus/event_source/devices > evidence/pmu-devices.txt; git rev-parse HEAD HEAD^{tree} > evidence/revision.txt; sha256sum src/buster/lib/compiler/frontend/c/c_parse.c src/buster/apps/ide/ide.c > evidence/source-before.sha256") == 0;
        if (ok) ok = run("clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o /tmp/ifetch-build > evidence/bootstrap.log 2>&1") == 0;
        if (ok) ok = run("/tmp/ifetch-build generate --cc clang --config Release --linker DEFAULT -- -DBUSTER_DEBUG_INFO=ON > evidence/configure.log 2>&1") == 0;
        if (ok) ok = build_variant("A",p,i);
        if (ok) ok = fixed_point();
        if (ok)
        {
            cases("A");
            const char *variants[] = {outline ? "N" : "Q", "C"};
            for (int v = 0; v < (outline ? 2 : 1) && ok; ++v)
            {
                ok = build_variant(variants[v],p,i);
                if (ok) cases(variants[v]);
            }
        }
        errors += run("cp build/compile_commands.json evidence/compile_commands.json; git diff --exit-code -- src/buster > evidence/restored.diff; sha256sum -c evidence/source-before.sha256 > evidence/source-restored.txt") != 0;
        if (ok && !outline) errors += run("evidence/ide-A test --module=c_frontend_tests,compiler_diagnostic_tests --ci=1 > evidence/targeted-tests.log 2>&1") != 0;
        fprintf(ledger,"RESEARCH_COMPLETE build_ok=%d workload_errors=%d timing=unmeasured\n",ok,errors);
    }
    free(p); free(i);
    if (ledger) fclose(ledger);
    return !ok || errors ? 1 : 0;
}
