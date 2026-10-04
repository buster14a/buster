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
    {"buster-debug", "source/build/Release/ide cc", "-O0 -g -fregister-allocator=fast -march=baseline -target x86_64-unknown-linux"},
    {"clang-debug", "clang", "-O0 -g -march=x86-64"},
    {"gcc-debug", "gcc", "-O0 -g -march=x86-64"},
    {"tcc-debug", "external/tcc/bin/tcc", "-O0 -g -m64"},
    {"clang-O2-g", "clang", "-O2 -g -march=x86-64"},
    {"gcc-O2-g", "gcc", "-O2 -g -march=x86-64"}
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
    snprintf(path, sizeof(path), "evidence/%s.log", name);
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
        snprintf(path, sizeof(path), "evidence/%s.c", sources[w]);
        FILE *f = fopen(path, "w");
        if (!f) { failures++; continue; }
        unsigned n = w == 0 ? 8 : w == 1 ? 256 : 2048;
        fputs("_Static_assert(sizeof(unsigned)==4,\"u32\");\n", f);
        for (unsigned i = 0; i < n; ++i)
            fprintf(f, "unsigned f%u(unsigned a,unsigned b){unsigned x=a+%uU;unsigned y=b^x;return (x*3U)+(y>>2);}\n", i, i+1);
        fclose(f);
    }
    FILE *f = fopen("evidence/kernels.c", "w");
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
    f = fopen("evidence/debug_probe.c", "w");
    if (f)
    {
        fputs("volatile int anchor;\nint debug_fn(int input)\n{\n    int saved = input + 2;\n    anchor = saved;\n    anchor += input;\n    return saved;\n}\nint main(void){return debug_fn(5)==7 ? 0 : 1;}\n", f);
        fclose(f);
    }
    else failures++;
    f = fopen("evidence/caller.c", "w");
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
    command("exec sh -c 'uname -a; lscpu; cat /proc/meminfo; cat /etc/os-release; printf \"ImageOS=%s ImageVersion=%s RUNNER_ARCH=%s\\n\" \"$ImageOS\" \"$ImageVersion\" \"$RUNNER_ARCH\"'", "host");
    command("exec sh -c 'clang --version; gcc -v; gdb --version; ld --version; cmake --version; ninja --version; dpkg-query -W clang gcc gdb binutils libc6; sha256sum $(command -v clang) $(command -v gcc)'", "toolchains");
    command("git init -q source && git -C source remote add origin https://github.com/buster14a/buster && git -C source fetch -q --depth=1 origin f34cc3a56296be1b1244647775e1a40a67dae3ef && git -C source checkout -q FETCH_HEAD", "source");
    command("git clone -q https://github.com/TinyCC/tinycc external/tinycc && git -C external/tinycc checkout -q 0fb54300b56512754221d80adda85ddb9815bceb", "tcc-source");
    command("cd external/tinycc && ./configure --prefix=\"$(pwd)/../tcc\" --cc=clang --extra-cflags='-O2' && make -j2 && make install", "tcc-build");
    command("cd source && clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -lm -o ../build-driver && ../build-driver generate --ci --no-sanitize --no-fuzz --no-lto --linker DEFAULT -- -DBUSTER_INCLUDE_TESTS=OFF -DBUSTER_UNITY_BUILD=ON && ../build-driver build --config Release -t ide", "buster-build");
    command("cp source/build/CMakeCache.txt source/build/compile_commands.json evidence/; cp source/AGENTS.md source/THIRD_PARTY_NOTICES.md evidence/; cp external/tinycc/COPYING evidence/tcc-COPYING; cp external/tinycc/README evidence/tcc-README; external/tcc/bin/tcc -v > evidence/tcc-version.txt; sha256sum source/build/Release/ide external/tcc/bin/tcc > evidence/compiler-sha256.txt; git -C source rev-parse HEAD HEAD^{tree} > evidence/source-pin.txt; git -C external/tinycc rev-parse HEAD HEAD^{tree} > evidence/tcc-pin.txt", "receipts");
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    mkdir("evidence", 0755); mkdir("external", 0755);
    setup();
    generate();
    command("exec gcc -O2 -g -march=x86-64 -fno-pie -c evidence/caller.c -o evidence/caller.o", "caller-build");
    FILE *csv = fopen("evidence/samples.csv", "w");
    if (!csv) failures++;
    if (csv) fputs("arm,work,round,wall_s,max_process_rss_kib,user_s,system_s,status,command\n", csv);
    int valid[6] = {0};
    for (unsigned arm = 0; arm < 6; ++arm)
    {
        char cmd[2048], name[256];
        snprintf(cmd, sizeof(cmd), "%s %s -funsigned-char -fno-strict-aliasing -fwrapv -fno-pie -c evidence/kernels.c -o evidence/%s-kernels.o", arms[arm].compiler, arms[arm].flags, arms[arm].name);
        snprintf(name, sizeof(name), "%s-preflight", arms[arm].name);
        valid[arm] = command(cmd, name) == 0;
        if (valid[arm])
        {
            snprintf(cmd, sizeof(cmd), "gcc -no-pie -Wl,--no-eh-frame-hdr evidence/caller.o evidence/%s-kernels.o -o evidence/%s-runtime", arms[arm].name, arms[arm].name);
            snprintf(name, sizeof(name), "%s-link", arms[arm].name);
            valid[arm] = command(cmd, name) == 0;
        }
        if (valid[arm])
        {
            snprintf(cmd, sizeof(cmd), "exec evidence/%s-runtime", arms[arm].name);
            snprintf(name, sizeof(name), "%s-oracle-warmup", arms[arm].name);
            valid[arm] = command(cmd, name) == 0;
        }
        snprintf(cmd, sizeof(cmd), "%s %s -funsigned-char -fno-strict-aliasing -fwrapv -fno-pie -no-pie evidence/debug_probe.c -o evidence/%s-debug", arms[arm].compiler, arms[arm].flags, arms[arm].name);
        snprintf(name, sizeof(name), "%s-debug-build", arms[arm].name);
        int debug_ok = command(cmd, name) == 0;
        if (debug_ok)
        {
            snprintf(cmd, sizeof(cmd), "exec gdb -nx -batch -ex 'break debug_probe.c:6' -ex run -ex 'print input' -ex 'print saved' -ex bt evidence/%s-debug", arms[arm].name);
            snprintf(name, sizeof(name), "%s-gdb", arms[arm].name);
            command(cmd, name);
        }
    }
    for (int round = -1; round < 14; ++round)
    {
        for (unsigned position = 0; position < 6; ++position)
        {
            unsigned arm = ((unsigned)(round + 1) / 2 + ((round & 1) ? 5 - position : position)) % 6;
            if (!valid[arm]) continue;
            for (unsigned w = 0; w < 4; ++w)
            {
                char cmd[2048], log[256], output[256];
                snprintf(output, sizeof(output), "evidence/%s-%s.o", arms[arm].name, sources[w]);
                unlink(output);
                snprintf(cmd, sizeof(cmd), "exec %s %s -funsigned-char -fno-strict-aliasing -fwrapv -fno-pie -c evidence/%s.c -o %s", arms[arm].compiler, arms[arm].flags, sources[w], output);
                snprintf(log, sizeof(log), "evidence/%s-%s-%d.log", arms[arm].name, sources[w], round);
                int status = measure(cmd, log, csv, arms[arm].name, sources[w], round);
                struct stat st;
                if (status || stat(output, &st) || st.st_size == 0) failures++;
                snprintf(cmd, sizeof(cmd), "sha256sum %s >> evidence/sample-artifact-hashes.txt", output);
                if (status == 0 && system(cmd)) failures++;
            }
            if (round >= 0)
            {
                char cmd[512], log[256];
                snprintf(cmd, sizeof(cmd), "exec evidence/%s-runtime", arms[arm].name);
                snprintf(log, sizeof(log), "evidence/%s-runtime-%d.log", arms[arm].name, round);
                int status = measure(cmd, log, csv, arms[arm].name, "runtime", round);
                if (status) failures++;
            }
        }
        fprintf(stderr, "round %d complete\n", round);
    }
    if (csv) fclose(csv);
    for (unsigned arm = 0; arm < 6; ++arm)
    {
        char cmd[1024], name[128];
        snprintf(cmd, sizeof(cmd), "readelf -SW evidence/%s-kernels.o; size -A evidence/%s-kernels.o; objdump -drwC -Mintel evidence/%s-kernels.o; readelf --debug-dump=info evidence/%s-kernels.o; size -A evidence/%s-tiny.o evidence/%s-functions256.o evidence/%s-functions2048.o", arms[arm].name, arms[arm].name, arms[arm].name, arms[arm].name, arms[arm].name, arms[arm].name, arms[arm].name);
        snprintf(name, sizeof(name), "%s-artifact", arms[arm].name);
        command(cmd, name);
    }
    command("sha256sum evidence/*.c evidence/*.o evidence/*-runtime > evidence/artifacts-sha256.txt", "artifact-hashes");
    /* A bounded text receipt also survives browser artifact-download restrictions.
     * Excludes executable objects: the normal uploaded artifact retains those. */
    if (system("tar -czf replay-receipt.tar.gz evidence/*.c evidence/*.csv evidence/*.txt evidence/*.log evidence/CMakeCache.txt evidence/compile_commands.json evidence/tcc-COPYING evidence/tcc-README && sha256sum replay-receipt.tar.gz && base64 -w 4000 replay-receipt.tar.gz")) failures++;
    fprintf(stderr, "capture failures=%d; debugger/unsupported failures are retained\n", failures);
    return failures != 0;
}
