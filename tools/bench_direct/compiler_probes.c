// Build and time the compiler-internal scaling probes of compiler-probes-v1 (#2761).
//
// Ownership: tools/bench_direct, trusted main only. compiler_compare.py
// compiles this file from the trusted checkout and runs
// `compiler_probes REPOSITORY BASE HEAD WORK OUTPUT` as the compiler-probes-v1
// leg of a pull comparison; the same command reproduces the leg by hand.
// `--self-test` runs synthetic checks without git, clang or pinning, and
// `--profile` prints the frozen profile as JSON so compiler_test.py can prove it
// equals compiler_receipt.COMPILER_PROBES_PROFILE.
//
// tools/fast_ra_scaling.c and tools/machine_line_mark_scaling.c link compiler
// internals, so they cannot be direct workloads. Following their audits, the
// head's probe source is copied outside both checkouts and compiled once
// against the merge base's src/ and once against the head's, each in an
// isolated shared clone, so the base checkout and its closure checks stay
// untouched. Every case then runs both binaries with fixed arguments as
// warmups and alternating pairs pinned to the profile's CPU. Nothing depends on
// the request or the timings. summary.json keeps every raw sample and each
// invocation's self-check fields; compiler_receipt.validate_compiler_probes
// re-derives every statistic from them, and compiler_compare hashes the
// binaries and copied sources itself.
//
// The trusted harness checkout holds only tools/, so this file includes no
// Buster library header and defines its own linkage macro, as other
// standalone tools do. It allocates only fixed-size records; no recursion, no
// callbacks, one return per function.
//
// Map: cp_probes (frozen profile), cp_run (bounded child process), cp_parse
// (one invocation's sample lines), cp_invoke, cp_case (warmups and pairs),
// cp_build_all (clones and builds), cp_leg, cp_print_profile, cp_self_test, main.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUSTER_GLOBAL_LOCAL static

#define CP_SCHEMA "buster-compiler-probes-v1"
#define CP_PROFILE_NAME "compiler-probes-v1"
#define CP_COMPILER "clang"
#define CP_CPU "2"
#define CP_REPEATS 11u
#define CP_WARMUPS 2u
#define CP_PAIRS 30u
// Shared with compiler_receipt.COMPILER_PROBES_INVOCATION_TIMEOUT_SECONDS, which
// also bounds every accepted sample.
#define CP_INVOCATION_TIMEOUT_SECONDS 60
// Measured: one probe build (machine.c dominates) took 35-38 s on a hosted 2.8 GHz Xeon.
#define CP_BUILD_TIMEOUT_SECONDS 600
#define CP_GIT_TIMEOUT_SECONDS 600
#define CP_OUTPUT_LIMIT (1u << 20)
#define CP_SAMPLE_CAPACITY 64u
#define CP_FIELD_CAPACITY 16u
#define CP_TEXT 64u
#define CP_PATH 4096u
#define CP_ARGUMENTS 48u
#define CP_ERRORS 32u
#define CP_ROLES 2u

typedef struct CpCase CpCase;
struct CpCase
{
    const char* name;
    const char* arguments[5];
};

typedef struct CpProbe CpProbe;
struct CpProbe
{
    const char* name;
    const char* source;
    const char* const* compile;
    const char* const* sources;
    const char* metric;
    const CpCase* cases;
    unsigned case_count;
};

typedef struct CpField CpField;
struct CpField
{
    char key[CP_TEXT];
    char value[CP_TEXT];
};

typedef struct CpInvocation CpInvocation;
struct CpInvocation
{
    int exit;
    int check_valid;
    unsigned sample_count;
    unsigned field_count;
    unsigned long long samples[CP_SAMPLE_CAPACITY];
    CpField fields[CP_FIELD_CAPACITY];
};

typedef struct CpLeg CpLeg;
struct CpLeg
{
    unsigned error_count;
    char errors[CP_ERRORS][256];
};

BUSTER_GLOBAL_LOCAL const char* const cp_roles[CP_ROLES] = {"baseline", "candidate"};

// The audits' commands: 2026-09-07T222944Z for the FAST probe, plus codegen.c,
// which machine.c needs since #2876; 2026-09-18T224655Z schedule-only for line-mark.
BUSTER_GLOBAL_LOCAL const char* const cp_fast_ra_compile[] = {
    "-O2", "-g", "-ffunction-sections", "-fdata-sections", "-DBUSTER_INCLUDE_TESTS=0", "-DBUSTER_LINK_LIBC=1",
    "-DBUSTER_OPTIMIZE=1", "-DBUSTER_SINGLE_THREADED=1", "-Isrc", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", 0,
};
BUSTER_GLOBAL_LOCAL const char* const cp_line_mark_compile[] = {
    "-O2", "-g", "-ffunction-sections", "-fdata-sections", "-Wall", "-Werror", "-Wno-unused-function",
    "-Wno-unused-variable", "-DBUSTER_INCLUDE_TESTS=0", "-DBUSTER_LINK_LIBC=1", "-DBUSTER_OPTIMIZE=1",
    "-DBUSTER_SINGLE_THREADED=1", "-DBUSTER_LINE_MARK_HELPER_BENCH=0", "-Isrc", "-fwrapv", "-fno-strict-aliasing",
    "-funsigned-char", 0,
};
BUSTER_GLOBAL_LOCAL const char* const cp_fast_ra_sources[] = {
    "src/buster/lib/compiler/codegen/machine.c", "src/buster/lib/compiler/codegen/codegen.c", "src/buster/lib/arena.c",
    "src/buster/lib/os.c", "src/buster/lib/integer.c", "src/buster/lib/string.c", "src/buster/lib/float.c",
    "src/buster/lib/time.c", 0,
};
BUSTER_GLOBAL_LOCAL const char* const cp_line_mark_sources[] = {
    "src/buster/lib/compiler/codegen/machine.c", "src/buster/lib/arena.c", "src/buster/lib/os.c",
    "src/buster/lib/integer.c", "src/buster/lib/string.c", "src/buster/lib/float.c", "src/buster/lib/time.c", 0,
};
BUSTER_GLOBAL_LOCAL const char* const cp_link[] = {"-Wl,--gc-sections", "-lpthread", "-ldl", "-lm", 0};
BUSTER_GLOBAL_LOCAL const CpCase cp_fast_ra_cases[] = {
    {"chain-256", {"256", "11", "chain", 0}}, {"chain-1024", {"1024", "11", "chain", 0}},
    {"chain-4096", {"4096", "11", "chain", 0}}, {"chain-16384", {"16384", "11", "chain", 0}},
    {"fanout-256", {"256", "11", "fanout", 0}}, {"fanout-1024", {"1024", "11", "fanout", 0}},
    {"fanout-4096", {"4096", "11", "fanout", 0}}, {"fanout-16384", {"16384", "11", "fanout", 0}},
};
BUSTER_GLOBAL_LOCAL const CpCase cp_line_mark_cases[] = {
    {"schedule-1024", {"schedule", "1024", "11", "1", 0}}, {"schedule-4096", {"schedule", "4096", "11", "1", 0}},
    {"schedule-16384", {"schedule", "16384", "11", "1", 0}}, {"schedule-65536", {"schedule", "65536", "11", "1", 0}},
};
BUSTER_GLOBAL_LOCAL const CpProbe cp_probes[] = {
    {"fast_ra", "tools/fast_ra_scaling.c", cp_fast_ra_compile, cp_fast_ra_sources, "total_ns", cp_fast_ra_cases,
     sizeof(cp_fast_ra_cases) / sizeof(cp_fast_ra_cases[0])},
    {"line_mark", "tools/machine_line_mark_scaling.c", cp_line_mark_compile, cp_line_mark_sources, "schedule_ns",
     cp_line_mark_cases, sizeof(cp_line_mark_cases) / sizeof(cp_line_mark_cases[0])},
};
#define CP_PROBE_COUNT (sizeof(cp_probes) / sizeof(cp_probes[0]))

BUSTER_GLOBAL_LOCAL double cp_clock(void)
{
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

BUSTER_GLOBAL_LOCAL void cp_error(CpLeg* leg, const char* message, const char* detail)
{
    if (leg->error_count < CP_ERRORS)
    {
        snprintf(leg->errors[leg->error_count], sizeof(leg->errors[0]), "%s%s", message, detail ? detail : "");
        leg->error_count += 1;
    }
}

BUSTER_GLOBAL_LOCAL void cp_quote(FILE* file, const char* text)
{
    fputc('"', file);
    for (const unsigned char* c = (const unsigned char*)text; *c; c += 1)
    {
        if (*c == '"' || *c == '\\')
        {
            fprintf(file, "\\%c", *c);
        }
        else if (*c < 0x20 || *c >= 0x7f)
        {
            fprintf(file, "\\u%04x", *c);
        }
        else
        {
            fputc(*c, file);
        }
    }
    fputc('"', file);
}

BUSTER_GLOBAL_LOCAL void cp_log_argv(int log, char* const* argv)
{
    if (log >= 0)
    {
        dprintf(log, "$");
        for (unsigned index = 0; argv[index]; index += 1)
        {
            dprintf(log, " %s", argv[index]);
        }
        dprintf(log, "\n");
    }
}

// Run argv in its own process group with stdout and stderr on the given
// descriptors; 124 on timeout, 127 when it cannot start, 128 + signal when killed.
// The whole group is killed afterwards so no descendant outlives the command.
BUSTER_GLOBAL_LOCAL int cp_run(char* const* argv, const char* cwd, int out, int err, int timeout_seconds)
{
    int status = 127;
    pid_t pid = fork();
    if (pid == 0)
    {
        setpgid(0, 0);
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0)
        {
            dup2(null, STDIN_FILENO);
        }
        if (out >= 0)
        {
            dup2(out, STDOUT_FILENO);
        }
        if (err >= 0)
        {
            dup2(err, STDERR_FILENO);
        }
        if (!cwd || chdir(cwd) == 0)
        {
            execvp(argv[0], argv);
        }
        _exit(127);
    }
    if (pid > 0)
    {
        setpgid(pid, pid);
        double deadline = cp_clock() + timeout_seconds;
        int raw = 0;
        int done = 0;
        int timed_out = 0;
        while (!done)
        {
            pid_t waited = waitpid(pid, &raw, WNOHANG);
            done = waited == pid || (waited < 0 && errno != EINTR);
            if (!done && cp_clock() >= deadline)
            {
                kill(-pid, SIGKILL);
                while (waitpid(pid, &raw, 0) < 0 && errno == EINTR)
                {
                }
                done = 1;
                timed_out = 1;
            }
            if (!done)
            {
                struct timespec pause = {0, 2000000};
                nanosleep(&pause, 0);
            }
        }
        kill(-pid, SIGKILL);
        status = timed_out ? 124 : WIFEXITED(raw) ? WEXITSTATUS(raw) : WIFSIGNALED(raw) ? 128 + WTERMSIG(raw) : 127;
    }
    return status;
}

BUSTER_GLOBAL_LOCAL int cp_logged(char* const* argv, const char* cwd, int log, int timeout_seconds)
{
    cp_log_argv(log, argv);
    int status = cp_run(argv, cwd, log, log, timeout_seconds);
    if (log >= 0)
    {
        dprintf(log, "exit %d\n", status);
    }
    return status;
}

BUSTER_GLOBAL_LOCAL int cp_token_text(const char* begin, size_t length)
{
    int valid = length > 0 && length < CP_TEXT;
    for (size_t index = 0; valid && index < length; index += 1)
    {
        valid = begin[index] > 0x20 && begin[index] < 0x7f;
    }
    return valid;
}

// Parse one invocation's stdout: one line per sample, each a run of key=value
// tokens. The metric must be a decimal integer; every other field except
// *_ns timings is the self-check and must be identical on every line.
BUSTER_GLOBAL_LOCAL void cp_parse(const char* text, const char* metric, CpInvocation* invocation)
{
    int valid = text[0] != 0;
    unsigned lines = 0;
    const char* line = text;
    invocation->sample_count = 0;
    invocation->field_count = 0;
    while (valid && *line)
    {
        const char* end = strchr(line, '\n');
        const char* stop = end ? end : line + strlen(line);
        CpField fields[CP_FIELD_CAPACITY];
        unsigned field_count = 0;
        int have_metric = 0;
        unsigned long long sample = 0;
        const char* token = line;
        valid = stop > line;
        while (valid && token < stop)
        {
            const char* space = memchr(token, ' ', (size_t)(stop - token));
            const char* token_end = space ? space : stop;
            const char* equals = memchr(token, '=', (size_t)(token_end - token));
            valid = equals && cp_token_text(token, (size_t)(equals - token)) &&
                    cp_token_text(equals + 1, (size_t)(token_end - equals - 1));
            if (valid)
            {
                size_t key_length = (size_t)(equals - token);
                size_t value_length = (size_t)(token_end - equals - 1);
                int is_metric = key_length == strlen(metric) && memcmp(token, metric, key_length) == 0;
                int is_timing = key_length > 3 && memcmp(token + key_length - 3, "_ns", 3) == 0;
                if (is_metric)
                {
                    valid = value_length <= 19 && strspn(equals + 1, "0123456789") >= value_length;
                    sample = valid ? strtoull(equals + 1, 0, 10) : 0;
                    have_metric = valid;
                }
                else if (!is_timing)
                {
                    valid = field_count < CP_FIELD_CAPACITY;
                    if (valid)
                    {
                        memcpy(fields[field_count].key, token, key_length);
                        fields[field_count].key[key_length] = 0;
                        memcpy(fields[field_count].value, equals + 1, value_length);
                        fields[field_count].value[value_length] = 0;
                        field_count += 1;
                    }
                }
            }
            token = token_end + (space ? 1 : 0);
        }
        valid = valid && have_metric && invocation->sample_count < CP_SAMPLE_CAPACITY;
        if (valid && lines == 0)
        {
            memcpy(invocation->fields, fields, sizeof(fields[0]) * field_count);
            invocation->field_count = field_count;
        }
        else if (valid)
        {
            valid = field_count == invocation->field_count &&
                    memcmp(fields, invocation->fields, sizeof(fields[0]) * field_count) == 0;
        }
        if (valid)
        {
            invocation->samples[invocation->sample_count] = sample;
            invocation->sample_count += 1;
            lines += 1;
        }
        line = end ? end + 1 : stop;
    }
    invocation->check_valid = valid;
}

BUSTER_GLOBAL_LOCAL char* cp_read_descriptor(int descriptor, size_t limit)
{
    char* text = malloc(limit + 1);
    size_t length = 0;
    if (text && lseek(descriptor, 0, SEEK_SET) == 0)
    {
        ssize_t received = 1;
        while (received > 0 && length < limit)
        {
            received = read(descriptor, text + length, limit - length);
            length += received > 0 ? (size_t)received : 0;
        }
    }
    if (text)
    {
        text[length] = 0;
    }
    return text;
}

// One invocation of binary with arguments, pinned to the profile's CPU unless
// pin is zero (the self-test). Failures append their output to log.
BUSTER_GLOBAL_LOCAL void cp_invoke(const char* binary, const char* const* arguments, const char* metric, int pin, int log,
                                   CpInvocation* invocation)
{
    char* argv[CP_ARGUMENTS];
    unsigned count = 0;
    if (pin)
    {
        argv[count++] = "taskset";
        argv[count++] = "-c";
        argv[count++] = CP_CPU;
    }
    argv[count++] = (char*)binary;
    for (unsigned index = 0; arguments[index]; index += 1)
    {
        argv[count++] = (char*)arguments[index];
    }
    argv[count] = 0;
    char out_name[] = "/tmp/compiler-probes-out-XXXXXX";
    char err_name[] = "/tmp/compiler-probes-err-XXXXXX";
    int out = mkstemp(out_name);
    int err = mkstemp(err_name);
    memset(invocation, 0, sizeof(*invocation));
    invocation->exit = 127;
    if (out >= 0 && err >= 0)
    {
        unlink(out_name);
        unlink(err_name);
        invocation->exit = cp_run(argv, 0, out, err, CP_INVOCATION_TIMEOUT_SECONDS);
        char* text = cp_read_descriptor(out, CP_OUTPUT_LIMIT);
        char* errors = cp_read_descriptor(err, CP_OUTPUT_LIMIT);
        cp_parse(text ? text : "", metric, invocation);
        if ((invocation->exit != 0 || !invocation->check_valid || (errors && errors[0])) && log >= 0)
        {
            cp_log_argv(log, argv);
            dprintf(log, "exit %d\n%s%s", invocation->exit, text ? text : "", errors ? errors : "");
        }
        free(text);
        free(errors);
    }
    if (out >= 0)
    {
        close(out);
    }
    if (err >= 0)
    {
        close(err);
    }
}

BUSTER_GLOBAL_LOCAL void cp_write_invocation(FILE* file, const CpInvocation* invocation)
{
    fprintf(file, "{\"exit\": %d, \"samples\": [", invocation->exit);
    for (unsigned index = 0; index < invocation->sample_count; index += 1)
    {
        fprintf(file, "%s%llu", index ? ", " : "", invocation->samples[index]);
    }
    fprintf(file, "], \"check\": ");
    if (invocation->check_valid)
    {
        fputc('{', file);
        for (unsigned index = 0; index < invocation->field_count; index += 1)
        {
            fprintf(file, "%s", index ? ", " : "");
            cp_quote(file, invocation->fields[index].key);
            fprintf(file, ": ");
            cp_quote(file, invocation->fields[index].value);
        }
        fputc('}', file);
    }
    else
    {
        fprintf(file, "null");
    }
    fputc('}', file);
}

// Pair index's first role: even pairs run the baseline first, odd pairs the candidate.
BUSTER_GLOBAL_LOCAL unsigned cp_pair_first(unsigned index)
{
    return index % 2u;
}

// One case: CP_WARMUPS warmups per binary alternating baseline, candidate, then
// CP_PAIRS pairs in the fixed order. Returns how many invocations failed.
BUSTER_GLOBAL_LOCAL unsigned cp_case(FILE* file, const char* const binaries[CP_ROLES], const CpProbe* probe,
                                     const CpCase* item, int pin, int log)
{
    unsigned failed = 0;
    CpInvocation invocation;
    CpInvocation warmups[CP_ROLES][CP_WARMUPS];
    for (unsigned warmup = 0; warmup < CP_WARMUPS; warmup += 1)
    {
        for (unsigned role = 0; role < CP_ROLES; role += 1)
        {
            cp_invoke(binaries[role], item->arguments, probe->metric, pin, log, &warmups[role][warmup]);
            failed += warmups[role][warmup].exit != 0 || !warmups[role][warmup].check_valid;
        }
    }
    fprintf(file, "{\"probe\": ");
    cp_quote(file, probe->name);
    fprintf(file, ", \"name\": ");
    cp_quote(file, item->name);
    fprintf(file, ", \"arguments\": [");
    for (unsigned index = 0; item->arguments[index]; index += 1)
    {
        fprintf(file, "%s", index ? ", " : "");
        cp_quote(file, item->arguments[index]);
    }
    fprintf(file, "], \"warmups\": {");
    for (unsigned role = 0; role < CP_ROLES; role += 1)
    {
        fprintf(file, "%s\"%s\": [", role ? ", " : "", cp_roles[role]);
        for (unsigned warmup = 0; warmup < CP_WARMUPS; warmup += 1)
        {
            fprintf(file, "%s", warmup ? ", " : "");
            cp_write_invocation(file, &warmups[role][warmup]);
        }
        fputc(']', file);
    }
    fprintf(file, "}, \"pairs\": [");
    for (unsigned pair = 0; pair < CP_PAIRS; pair += 1)
    {
        unsigned first = cp_pair_first(pair);
        fprintf(file, "%s{\"order\": [\"%s\", \"%s\"]", pair ? ", " : "", cp_roles[first], cp_roles[1u - first]);
        for (unsigned step = 0; step < CP_ROLES; step += 1)
        {
            unsigned role = step == 0 ? first : 1u - first;
            cp_invoke(binaries[role], item->arguments, probe->metric, pin, log, &invocation);
            failed += invocation.exit != 0 || !invocation.check_valid;
            fprintf(file, ", \"%s\": ", cp_roles[role]);
            cp_write_invocation(file, &invocation);
        }
        fputc('}', file);
    }
    fprintf(file, "]}");
    return failed;
}

BUSTER_GLOBAL_LOCAL int cp_copy_file(const char* from, const char* to)
{
    int ok = 0;
    FILE* in = fopen(from, "rb");
    FILE* out = in ? fopen(to, "wb") : 0;
    if (in && out)
    {
        char buffer[65536];
        size_t length = 1;
        ok = 1;
        while (ok && length > 0)
        {
            length = fread(buffer, 1, sizeof(buffer), in);
            ok = fwrite(buffer, 1, length, out) == length;
        }
        ok = ok && !ferror(in);
    }
    if (out && fclose(out) != 0)
    {
        ok = 0;
    }
    if (in)
    {
        fclose(in);
    }
    return ok;
}

// An over-long path becomes empty, so the step that uses it fails instead of
// touching a truncated path.
BUSTER_GLOBAL_LOCAL void cp_path(char* out, const char* directory, const char* name)
{
    int written = snprintf(out, CP_PATH, "%s/%s", directory, name);
    if (written < 0 || written >= (int)CP_PATH)
    {
        out[0] = 0;
    }
}

// Clone both revisions and build every probe against each. built[probe][role]
// is nonzero for a binary that exists; build_status records each exit.
BUSTER_GLOBAL_LOCAL void cp_build_all(const char* repository, const char* const revisions[CP_ROLES], const char* work,
                                      const char* output, int log, CpLeg* leg, int built[][CP_ROLES],
                                      int build_status[][CP_ROLES])
{
    char roots[CP_ROLES][CP_PATH];
    int cloned[CP_ROLES] = {0};
    for (unsigned role = 0; role < CP_ROLES; role += 1)
    {
        char name[64];
        snprintf(name, sizeof(name), "probes-%s", cp_roles[role]);
        cp_path(roots[role], work, name);
        char* clone[] = {"git", "-c", "core.hooksPath=/dev/null", "clone", "--quiet", "--shared", "--no-checkout",
                         (char*)repository, roots[role], 0};
        char* checkout[] = {"git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null",
                            "-C", roots[role], "checkout", "--quiet", "--detach", (char*)revisions[role], 0};
        cloned[role] = cp_logged(clone, 0, log, CP_GIT_TIMEOUT_SECONDS) == 0 &&
                       cp_logged(checkout, 0, log, CP_GIT_TIMEOUT_SECONDS) == 0;
        if (!cloned[role])
        {
            cp_error(leg, "revision could not be checked out (see build.log): ", revisions[role]);
        }
    }
    char sources[CP_PATH];
    cp_path(sources, work, "probe-sources");
    mkdir(sources, 0755);
    for (unsigned probe = 0; probe < CP_PROBE_COUNT; probe += 1)
    {
        char from[CP_PATH];
        char copy[CP_PATH];
        const char* base = strrchr(cp_probes[probe].source, '/') + 1;
        cp_path(from, roots[1], cp_probes[probe].source);
        cp_path(copy, sources, base);
        int copied = cloned[1] && cp_copy_file(from, copy);
        for (unsigned role = 0; role < CP_ROLES; role += 1)
        {
            char binary[CP_PATH];
            char name[64];
            snprintf(name, sizeof(name), "bin/%s-%s", cp_probes[probe].name, cp_roles[role]);
            cp_path(binary, output, name);
            char* argv[CP_ARGUMENTS];
            unsigned count = 0;
            argv[count++] = CP_COMPILER;
            for (unsigned index = 0; cp_probes[probe].compile[index]; index += 1)
            {
                argv[count++] = (char*)cp_probes[probe].compile[index];
            }
            argv[count++] = copy;
            for (unsigned index = 0; cp_probes[probe].sources[index]; index += 1)
            {
                argv[count++] = (char*)cp_probes[probe].sources[index];
            }
            for (unsigned index = 0; cp_link[index]; index += 1)
            {
                argv[count++] = (char*)cp_link[index];
            }
            argv[count++] = "-o";
            argv[count++] = binary;
            argv[count] = 0;
            build_status[probe][role] = cloned[role] && copied ?
                cp_logged(argv, roots[role], log, CP_BUILD_TIMEOUT_SECONDS) : 127;
            built[probe][role] = build_status[probe][role] == 0 && access(binary, X_OK) == 0;
            if (!built[probe][role])
            {
                char detail[128];
                snprintf(detail, sizeof(detail), "%s %s exited %d (see build.log)", cp_probes[probe].name,
                         cp_roles[role], build_status[probe][role]);
                cp_error(leg, "build failed: ", detail);
            }
        }
    }
}

// The whole leg: build, time every case, and write OUTPUT/summary.json. Returns
// 0 only for a complete run.
BUSTER_GLOBAL_LOCAL int cp_leg(const char* repository, const char* base, const char* head, const char* work,
                               const char* output)
{
    CpLeg leg = {0};
    const char* revisions[CP_ROLES] = {base, head};
    int built[CP_PROBE_COUNT][CP_ROLES] = {{0}};
    int build_status[CP_PROBE_COUNT][CP_ROLES] = {{0}};
    char path[CP_PATH];
    mkdir(output, 0755);
    cp_path(path, output, "bin");
    mkdir(path, 0755);
    int fresh = mkdir(work, 0755) == 0;
    cp_path(path, output, "build.log");
    int log = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (!fresh)
    {
        cp_error(&leg, "work directory must not exist yet: ", work);
    }
    else
    {
        cp_build_all(repository, revisions, work, output, log, &leg, built, build_status);
    }
    int complete = fresh;
    for (unsigned probe = 0; probe < CP_PROBE_COUNT; probe += 1)
    {
        complete = complete && built[probe][0] && built[probe][1];
    }
    char partial[CP_PATH];
    cp_path(partial, output, "summary.json.partial");
    FILE* file = fopen(partial, "w");
    int status = 1;
    if (file)
    {
        fprintf(file, "{\"schema\": \"%s\", \"profile\": \"%s\", \"base\": ", CP_SCHEMA, CP_PROFILE_NAME);
        cp_quote(file, base);
        fprintf(file, ", \"head\": ");
        cp_quote(file, head);
        fprintf(file, ", \"builds\": [");
        for (unsigned probe = 0; probe < CP_PROBE_COUNT; probe += 1)
        {
            for (unsigned role = 0; role < CP_ROLES; role += 1)
            {
                fprintf(file, "%s{\"probe\": \"%s\", \"role\": \"%s\", \"revision\": ", probe || role ? ", " : "",
                        cp_probes[probe].name, cp_roles[role]);
                cp_quote(file, revisions[role]);
                fprintf(file, ", \"exit\": %d}", build_status[probe][role]);
            }
        }
        fprintf(file, "], \"cases\": [");
        cp_path(path, output, "invocations.log");
        int invocations = complete ? open(path, O_WRONLY | O_CREAT | O_APPEND, 0644) : -1;
        unsigned failed = 0;
        unsigned written = 0;
        for (unsigned probe = 0; complete && probe < CP_PROBE_COUNT; probe += 1)
        {
            char binaries[CP_ROLES][CP_PATH];
            const char* paths[CP_ROLES];
            for (unsigned role = 0; role < CP_ROLES; role += 1)
            {
                char name[64];
                snprintf(name, sizeof(name), "bin/%s-%s", cp_probes[probe].name, cp_roles[role]);
                cp_path(binaries[role], output, name);
                paths[role] = binaries[role];
            }
            for (unsigned item = 0; item < cp_probes[probe].case_count; item += 1)
            {
                fprintf(file, "%s", written ? ", " : "");
                failed += cp_case(file, paths, &cp_probes[probe], &cp_probes[probe].cases[item], 1, invocations);
                written += 1;
            }
        }
        if (invocations >= 0)
        {
            close(invocations);
        }
        if (failed)
        {
            cp_error(&leg, "a case has failed, partial or inconsistent invocations (see invocations.log)", 0);
        }
        int succeeded = complete && leg.error_count == 0;
        fprintf(file, "], \"status\": \"%s\", \"errors\": [", succeeded ? "complete" : "failed");
        for (unsigned index = 0; index < leg.error_count; index += 1)
        {
            fprintf(file, "%s", index ? ", " : "");
            cp_quote(file, leg.errors[index]);
        }
        fprintf(file, "]}\n");
        int written_ok = !ferror(file);
        written_ok = fclose(file) == 0 && written_ok;
        cp_path(path, output, "summary.json");
        status = written_ok && rename(partial, path) == 0 && succeeded ? 0 : 1;
    }
    if (log >= 0)
    {
        close(log);
    }
    return status;
}

BUSTER_GLOBAL_LOCAL void cp_print_list(const char* const* items)
{
    fputc('[', stdout);
    for (unsigned index = 0; items[index]; index += 1)
    {
        fprintf(stdout, "%s", index ? ", " : "");
        cp_quote(stdout, items[index]);
    }
    fputc(']', stdout);
}

// The frozen profile as JSON, compared field by field with compiler_receipt.COMPILER_PROBES_PROFILE.
BUSTER_GLOBAL_LOCAL int cp_print_profile(void)
{
    printf("{\"name\": \"%s\", \"compiler\": \"%s\", \"cpu\": %s, \"repeats\": %u, \"warmups\": %u, \"pairs\": %u, "
           "\"invocation_timeout_seconds\": %d, \"probes\": {",
           CP_PROFILE_NAME, CP_COMPILER, CP_CPU, CP_REPEATS, CP_WARMUPS, CP_PAIRS, CP_INVOCATION_TIMEOUT_SECONDS);
    for (unsigned probe = 0; probe < CP_PROBE_COUNT; probe += 1)
    {
        printf("%s\"%s\": {\"source\": \"%s\", \"metric\": \"%s\", \"compile\": ", probe ? ", " : "",
               cp_probes[probe].name, cp_probes[probe].source, cp_probes[probe].metric);
        cp_print_list(cp_probes[probe].compile);
        printf(", \"sources\": ");
        cp_print_list(cp_probes[probe].sources);
        printf(", \"link\": ");
        cp_print_list(cp_link);
        printf(", \"cases\": [");
        for (unsigned item = 0; item < cp_probes[probe].case_count; item += 1)
        {
            printf("%s{\"name\": \"%s\", \"arguments\": ", item ? ", " : "", cp_probes[probe].cases[item].name);
            cp_print_list(cp_probes[probe].cases[item].arguments);
            putchar('}');
        }
        printf("]}");
    }
    printf("}}\n");
    return fflush(stdout) == 0 ? 0 : 1;
}

// A stand-in probe for the self-test: "ok" prints CP_REPEATS consistent lines,
// "drift" changes a self-check field on its last line, and "fail" exits 3.
BUSTER_GLOBAL_LOCAL int cp_fake_probe(const char* mode)
{
    int status = strcmp(mode, "fail") == 0 ? 3 : 0;
    for (unsigned line = 0; status == 0 && line < CP_REPEATS; line += 1)
    {
        int drift = strcmp(mode, "drift") == 0 && line + 1 == CP_REPEATS;
        printf("blocks=256 prepass_ns=%u total_ns=%u spills=%d frame=0\n", 10 + line, 100 + line, drift);
    }
    return status;
}

BUSTER_GLOBAL_LOCAL unsigned cp_check(unsigned failures, int condition, const char* name)
{
    if (!condition)
    {
        fprintf(stderr, "COMPILER_PROBES_SELF_TEST_FAIL %s\n", name);
    }
    return failures + !condition;
}

BUSTER_GLOBAL_LOCAL int cp_self_test(const char* self)
{
    unsigned failures = 0;
    CpInvocation invocation;
    const char* line = "blocks=256 prepass_ns=10 placement_ns=20 total_ns=30 spills=0 frame=0\n";
    char text[2048];
    snprintf(text, sizeof(text), "%s%s%s", line, line, line);
    cp_parse(text, "total_ns", &invocation);
    failures = cp_check(failures, invocation.check_valid && invocation.sample_count == 3 && invocation.samples[2] == 30 &&
                                      invocation.field_count == 3 && strcmp(invocation.fields[0].key, "blocks") == 0 &&
                                      strcmp(invocation.fields[2].value, "0") == 0, "consistent lines parse");
    const char* invalid[] = {
        "",
        "blocks=256 prepass_ns=10 spills=0\n",
        "blocks=256 total_ns=fast spills=0\n",
        "blocks=256 total_ns=30 spills=0\nerror\n",
        "blocks=256 total_ns=30 spills=0\nblocks=256 total_ns=30 spills=1\n",
        "blocks=256 total_ns=30 spills=0\n\nblocks=256 total_ns=30 spills=0\n",
        "blocks=256 total_ns=99999999999999999999 spills=0\n",
        "blocks=256  total_ns=30\n",
    };
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index += 1)
    {
        cp_parse(invalid[index], "total_ns", &invocation);
        failures = cp_check(failures, !invocation.check_valid, invalid[index]);
    }
    failures = cp_check(failures, cp_pair_first(0) == 0 && cp_pair_first(1) == 1 && cp_pair_first(28) == 0,
                        "pairs alternate starting with the baseline");
    char buffer[256] = {0};
    FILE* memory = fmemopen(buffer, sizeof(buffer), "w");
    if (memory)
    {
        cp_quote(memory, "a\"b\\c\n\x7f");
        fclose(memory);
    }
    failures = cp_check(failures, strcmp(buffer, "\"a\\\"b\\\\c\\u000a\\u007f\"") == 0, "JSON strings are escaped");
    const char* ok[] = {"--fake-probe", "ok", 0};
    const char* drift[] = {"--fake-probe", "drift", 0};
    const char* fail[] = {"--fake-probe", "fail", 0};
    cp_invoke(self, ok, "total_ns", 0, -1, &invocation);
    failures = cp_check(failures, invocation.exit == 0 && invocation.check_valid && invocation.sample_count == CP_REPEATS &&
                                      invocation.samples[0] == 100, "a passing probe invocation is recorded");
    cp_invoke(self, drift, "total_ns", 0, -1, &invocation);
    failures = cp_check(failures, invocation.exit == 0 && !invocation.check_valid, "a drifting self-check is refused");
    cp_invoke(self, fail, "total_ns", 0, -1, &invocation);
    failures = cp_check(failures, invocation.exit == 3, "a failing probe keeps its exit status");
    char* missing[] = {"/nonexistent/compiler-probes-self-test", 0};
    failures = cp_check(failures, cp_run(missing, 0, -1, -1, 5) == 127, "a missing program exits 127");
    // A whole case: 2 x (warmups + pairs) invocations, written as one JSON object.
    char* json = 0;
    size_t json_length = 0;
    FILE* stream = open_memstream(&json, &json_length);
    const char* binaries[CP_ROLES] = {self, self};
    CpCase fake = {"fake", {"--fake-probe", "ok", 0}};
    CpProbe probe = cp_probes[0];
    unsigned failed = stream ? cp_case(stream, binaries, &probe, &fake, 0, -1) : 1;
    if (stream)
    {
        fclose(stream);
    }
    unsigned objects = 0;
    for (const char* at = json; at && (at = strstr(at, "{\"exit\": 0")); at += 1)
    {
        objects += 1;
    }
    failures = cp_check(failures, failed == 0 && objects == CP_ROLES * (CP_WARMUPS + CP_PAIRS) && json &&
                                      strstr(json, "\"order\": [\"candidate\", \"baseline\"]"),
                        "a case writes every warmup and alternating pair");
    free(json);
    printf("COMPILER_PROBES_SELF_TEST %s failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    int status = 2;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        status = cp_self_test("/proc/self/exe");
    }
    else if (argc == 2 && strcmp(argv[1], "--profile") == 0)
    {
        status = cp_print_profile();
    }
    else if (argc == 3 && strcmp(argv[1], "--fake-probe") == 0)
    {
        status = cp_fake_probe(argv[2]);
    }
    else if (argc == 6)
    {
        status = cp_leg(argv[1], argv[2], argv[3], argv[4], argv[5]);
    }
    else
    {
        fprintf(stderr, "usage: %s REPOSITORY BASE HEAD WORK OUTPUT | --self-test | --profile\n", argv[0]);
    }
    return status;
}
