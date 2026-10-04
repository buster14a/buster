#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>

/* Standalone diagnostic capture; no Buster implementation or dependencies.
 * setup() freezes a hosted build; measure() uses blocking wait4 plus an
 * independent process-group watchdog. RSS is the largest waited process,
 * not simultaneous tree memory. Parent polling is never in the timer.
 */
typedef struct Arm { const char *name; const char *compiler; const char *flags; } Arm;
static Arm arms[] = {
    {"buster-g0", "source/build/Release/ide cc", "-O0 -g0 -fregister-allocator=fast -march=baseline -target x86_64-unknown-linux"},
    {"cproc-qbe-g0", "external/cproc/cproc", ""},
    {"clang-O2-g0", "clang", "-O2 -g0 -march=x86-64"}
};
static const char *sources[] = {"tiny", "functions256", "functions2048", "kernels"};
static int failures;
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static int measure(const char *cmd, const char *log, FILE *csv, const char *arm, const char *work, int round)
{
    int status = 0;
    struct rusage usage = {0};
    double start = now();
    pid_t child = fork();
    if (child == 0)
    {
        setpgid(0, 0);
        int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(125);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(126);
    }
    int result = 125;
    if (child > 0)
    {
        setpgid(child, child);
        pid_t watchdog = fork();
        if (watchdog == 0)
        {
            struct timespec limit = {csv ? 120 : 600, 0};
            nanosleep(&limit, 0);
            kill(-child, SIGKILL);
            _exit(0);
        }
        pid_t waited;
        do { waited = wait4(child, &status, 0, &usage); } while (waited < 0 && errno == EINTR);
        double elapsed = now() - start;
        if (waited == child)
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (watchdog > 0)
        {
            kill(watchdog, SIGKILL);
            waitpid(watchdog, 0, 0);
        }
        else result = 125;
        if (csv)
        {
            fprintf(csv, "%s,%s,%d,%.9f,%ld,%.9f,%.9f,%d,\"%s\"\n", arm, work, round, elapsed,
                    usage.ru_maxrss, (double)usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1e-6,
                    (double)usage.ru_stime.tv_sec + usage.ru_stime.tv_usec * 1e-6, result, cmd);
            fflush(csv);
        }
    }
    return result;
}
static int command(const char *cmd, const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "secondary-evidence/%s.log", name);
    int result = measure(cmd, path, 0, "", "", 0);
    fprintf(stderr, "%s: %d\n", name, result);
    if (result) failures++;
    return result;
}
static void generate(void)
{
    for (unsigned w = 0; w < 3; ++w)
    {
        char path[128];
        snprintf(path, sizeof(path), "secondary-evidence/%s.c", sources[w]);
        FILE *f = fopen(path, "w");
        if (!f) { failures++; continue; }
        unsigned n = w == 0 ? 8 : w == 1 ? 256 : 2048;
        fputs("_Static_assert(sizeof(unsigned)==4,\"u32\");\n", f);
        for (unsigned i = 0; i < n; ++i)
            fprintf(f, "unsigned f%u(unsigned a,unsigned b){unsigned x=a+%uU;unsigned y=b^x;return (x*3U)+(y>>2);}\n", i, i+1);
        fclose(f);
    }
    FILE *f = fopen("secondary-evidence/kernels.c", "w");
    if (f)
    {
        fputs("_Static_assert(sizeof(unsigned)==4,\"u32\");\n"
              "unsigned reduce(const unsigned *p,unsigned n){unsigned s=7;for(unsigned i=0;i<n;i++)s+=p[i]*3U;return s;}\n"
              "unsigned index_sum(const unsigned *p,unsigned n){unsigned s=0;for(unsigned i=0;i<n;i++)s+=p[(i*17U)&255U]^s;return s;}\n"
              "unsigned divide(const unsigned *p,unsigned n){unsigned s=0;for(unsigned i=0;i<n;i++)s+=p[i]/7U;return s;}\n"
              "double floating(const double *p,unsigned n){double s=0;for(unsigned i=0;i<n;i++)s+=(p[i]+1.0)*0.5;return s;}\n", f);
        fclose(f);
    }
    else failures++;
    f = fopen("secondary-evidence/debug_probe.c", "w");
    if (f)
    {
        fputs("volatile int anchor;\nint debug_fn(int input)\n{\n    int saved = input + 2;\n    anchor = saved;\n    anchor += input;\n    return saved;\n}\nint main(void){return debug_fn(5)==7 ? 0 : 1;}\n", f);
        fclose(f);
    }
    else failures++;
    f = fopen("secondary-evidence/caller.c", "w");
    if (f)
    {
        fputs("#define _POSIX_C_SOURCE 200809L\n#include <stdio.h>\n#include <time.h>\n#include <string.h>\n"
              "unsigned reduce(const unsigned*,unsigned);unsigned index_sum(const unsigned*,unsigned);unsigned divide(const unsigned*,unsigned);double floating(const double*,unsigned);\n"
              "static double clock_s(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}\n"
              "int main(void){unsigned p[256];double q[256];for(unsigned i=0;i<256;i++){p[i]=(i*2654435761U)^12345U;q[i]=(double)i*0.25;}\n"
              "unsigned lengths[]={0,1,2,7,255,256};int bad=0;for(unsigned k=0;k<6;k++){unsigned n=lengths[k],s=7,t=0,u=0;double d=0;for(unsigned i=0;i<n;i++){s+=p[i]*3U;t+=p[(i*17U)&255U]^t;u+=p[i]/7U;d+=(q[i]+1.0)*0.5;}if(reduce(p,n)!=s||index_sum(p,n)!=t||divide(p,n)!=u||floating(q,n)!=d)bad=1;}\n"
              "if(bad){puts(\"ORACLE_FAIL\");return 1;}puts(\"ORACLE_PASS 24 checks\");volatile unsigned sink=0;volatile double dsink=0;unsigned reps=131072;double a=clock_s();for(unsigned i=0;i<reps;i++)sink+=reduce(p,256);double b=clock_s();printf(\"reduce %.9f %u\\n\",b-a,sink);\n"
              "a=clock_s();for(unsigned i=0;i<reps;i++)sink+=index_sum(p,256);b=clock_s();printf(\"index %.9f %u\\n\",b-a,sink);\n"
              "a=clock_s();for(unsigned i=0;i<reps;i++)sink+=divide(p,256);b=clock_s();printf(\"divide %.9f %u\\n\",b-a,sink);\n"
              "a=clock_s();for(unsigned i=0;i<reps;i++)dsink+=floating(q,256);b=clock_s();printf(\"floating %.9f %.17g\\n\",b-a,dsink);return 0;}\n", f);
        fclose(f);
    }
    else failures++;
}
static void setup(void)
{
    command("curl -fsSL https://c9x.me/compile/release/qbe-1.3.tar.xz -o external/qbe-1.3.tar.xz && echo 'd587905d620dc5e1d2bfa7c2cc642b9b837aa89a3188c6e37b53d756cf66e320  external/qbe-1.3.tar.xz' | sha256sum -c - && tar -xf external/qbe-1.3.tar.xz -C external", "qbe-source");
    command("cp external/qbe-1.3/COPYING secondary-evidence/qbe-COPYING; cp external/qbe-1.3/README secondary-evidence/qbe-README; cat external/qbe-1.3/COPYING", "qbe-license");
    command("cd external/qbe-1.3 && make -j2 CC=clang CFLAGS='-O2 -g'", "qbe-build");
    command("git clone -q https://github.com/michaelforney/cproc external/cproc && git -C external/cproc checkout -q d1c53ddf56571573a7025324c8dd5c6d547a4d1f", "cproc-source");
    command("cp external/cproc/LICENSE secondary-evidence/cproc-LICENSE; cat external/cproc/LICENSE", "cproc-license");
    command("cd external/cproc && ./configure CC=clang CFLAGS='-O2 -g' --target=x86_64-linux-gnu --with-qbe=\"$(pwd)/../qbe-1.3/qbe\" --with-cpp=cpp --with-as=as --with-ld=ld && make -j2", "cproc-build");
    command("cp external/cproc/config.h external/cproc/config.mk secondary-evidence/; cp source/build/CMakeCache.txt source/build/compile_commands.json secondary-evidence/; git -C external/cproc rev-parse HEAD HEAD^{tree} > secondary-evidence/cproc-pin.txt; sha256sum source/build/Release/ide external/cproc/cproc external/cproc/cproc-qbe external/qbe-1.3/qbe > secondary-evidence/compiler-sha256.txt", "receipts");
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    mkdir("secondary-evidence", 0755); mkdir("external", 0755);
    setup();
    generate();
    command("exec gcc -O2 -g -march=x86-64 -fno-pie -c secondary-evidence/caller.c -o secondary-evidence/caller.o", "caller-build");
    FILE *csv = fopen("secondary-evidence/samples.csv", "w");
    if (!csv) failures++;
    if (csv) fputs("arm,work,round,wall_s,max_process_rss_kib,user_s,system_s,status,command\n", csv);
    int valid[6] = {0};
    for (unsigned arm = 0; arm < 3; ++arm)
    {
        char cmd[2048], name[256];
        snprintf(cmd, sizeof(cmd), "%s %s %s -c secondary-evidence/kernels.c -o secondary-evidence/%s-kernels.o", arms[arm].compiler, arms[arm].flags, arm == 1 ? "" : "-funsigned-char -fno-strict-aliasing -fwrapv -fno-pie", arms[arm].name);
        snprintf(name, sizeof(name), "%s-preflight", arms[arm].name);
        valid[arm] = command(cmd, name) == 0;
        if (valid[arm])
        {
            snprintf(cmd, sizeof(cmd), "gcc -no-pie -Wl,--no-eh-frame-hdr secondary-evidence/caller.o secondary-evidence/%s-kernels.o -o secondary-evidence/%s-runtime", arms[arm].name, arms[arm].name);
            snprintf(name, sizeof(name), "%s-link", arms[arm].name);
            valid[arm] = command(cmd, name) == 0;
        }
        if (valid[arm])
        {
            snprintf(cmd, sizeof(cmd), "exec secondary-evidence/%s-runtime", arms[arm].name);
            snprintf(name, sizeof(name), "%s-oracle-warmup", arms[arm].name);
            valid[arm] = command(cmd, name) == 0;
        }

    }
    for (int round = -1; round < 14; ++round)
    {
        for (unsigned position = 0; position < 3; ++position)
        {
            unsigned arm = ((unsigned)(round + 1) / 2 + ((round & 1) ? 2 - position : position)) % 3;
            if (!valid[arm]) continue;
            for (unsigned w = 0; w < 4; ++w)
            {
                char cmd[2048], log[256], output[256];
                snprintf(output, sizeof(output), "secondary-evidence/%s-%s.o", arms[arm].name, sources[w]);
                unlink(output);
                snprintf(cmd, sizeof(cmd), "exec %s %s %s -c secondary-evidence/%s.c -o %s", arms[arm].compiler, arms[arm].flags, arm == 1 ? "" : "-funsigned-char -fno-strict-aliasing -fwrapv -fno-pie", sources[w], output);
                snprintf(log, sizeof(log), "secondary-evidence/%s-%s-%d.log", arms[arm].name, sources[w], round);
                int status = measure(cmd, log, csv, arms[arm].name, sources[w], round);
                struct stat st;
                if (status || stat(output, &st) || st.st_size == 0) failures++;
                snprintf(cmd, sizeof(cmd), "sha256sum %s >> secondary-evidence/sample-artifact-hashes.txt", output);
                if (status == 0 && system(cmd)) failures++;
            }
            if (round >= 0)
            {
                char cmd[512], log[256];
                snprintf(cmd, sizeof(cmd), "exec secondary-evidence/%s-runtime", arms[arm].name);
                snprintf(log, sizeof(log), "secondary-evidence/%s-runtime-%d.log", arms[arm].name, round);
                int status = measure(cmd, log, csv, arms[arm].name, "runtime", round);
                if (status) failures++;
            }
        }
        fprintf(stderr, "round %d complete\n", round);
    }
    if (csv) fclose(csv);
    for (unsigned arm = 0; arm < 3; ++arm)
    {
        char cmd[1024], name[128];
        snprintf(cmd, sizeof(cmd), "readelf -SW secondary-evidence/%s-kernels.o; size -A secondary-evidence/%s-kernels.o; objdump -drwC -Mintel secondary-evidence/%s-kernels.o; readelf --debug-dump=info secondary-evidence/%s-kernels.o; size -A secondary-evidence/%s-tiny.o secondary-evidence/%s-functions256.o secondary-evidence/%s-functions2048.o", arms[arm].name, arms[arm].name, arms[arm].name, arms[arm].name, arms[arm].name, arms[arm].name, arms[arm].name);
        snprintf(name, sizeof(name), "%s-artifact", arms[arm].name);
        command(cmd, name);
    }
    command("sha256sum secondary-evidence/*.c secondary-evidence/*.o secondary-evidence/*-runtime > secondary-evidence/artifacts-sha256.txt", "artifact-hashes");
    /* A bounded text receipt also survives browser artifact-download restrictions.
     * Excludes executable objects: the normal uploaded artifact retains those. */
    if (system("tar -czf secondary-replay-receipt.tar.gz secondary-evidence/*.c secondary-evidence/*.csv secondary-evidence/*.txt secondary-evidence/*.log secondary-evidence/CMakeCache.txt secondary-evidence/compile_commands.json secondary-evidence/qbe-COPYING secondary-evidence/qbe-README secondary-evidence/cproc-LICENSE secondary-evidence/config.h secondary-evidence/config.mk && sha256sum secondary-replay-receipt.tar.gz && base64 -w 4000 secondary-replay-receipt.tar.gz")) failures++;
    fprintf(stderr, "capture failures=%d; debugger/unsupported failures are retained\n", failures);
    return failures != 0;
}
