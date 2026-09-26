// Research-only POSIX hosted probe. No production source or test registry changes.
// Build with the hosted C compiler; run: probe /absolute/path/to/ide NEW_DIRECTORY
// Compares real compiler processes, never a copied implementation. No timing claim.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct Spec Spec;
struct Spec
{
    unsigned pack, prefix, tail, noise;
    unsigned header, macro, extra, passes, explicit_phase, jobs;
    unsigned natural_sentinel, paired, kind; // kind: pragma=0, attribute=1, natural=2
};
typedef struct Observation Observation;
struct Observation
{
    bool ready, equivalent, violation;
    char directory[1024], expected[128], natural[128], actual[128];
    char split[1024];
};
static char const* ide;
static char const* root;
static char const* allocator = "fast";
static bool frontend_ssa = true;
static unsigned serial, pairs, violations, setup_errors, control_failures, accepts, replays;

static bool make_path(char* out, size_t capacity, char const* directory, char const* name)
{
    int n = snprintf(out, capacity, "%s/%s", directory, name);
    return n >= 0 && (size_t)n < capacity;
}

static bool read_text(char const* path, char* out, size_t capacity)
{
    bool ok = false;
    FILE* file = fopen(path, "rb");
    if (file)
    {
        size_t n = fread(out, 1, capacity - 1, file);
        out[n] = 0;
        ok = !ferror(file) && fgetc(file) == EOF;
        if (fclose(file)) ok = false;
    }
    return ok;
}

static bool nonempty_file(char const* path)
{
    struct stat st;
    bool ok = stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
    return ok;
}

static bool run_command(char const* directory, char const* label, char const* const* arguments,
                        char* output, size_t output_capacity)
{
    char name[128], out_path[1200], err_path[1200], record_path[1200];
    snprintf(name, sizeof(name), "%s.stdout", label);
    bool ok = make_path(out_path, sizeof(out_path), directory, name);
    snprintf(name, sizeof(name), "%s.stderr", label);
    ok = make_path(err_path, sizeof(err_path), directory, name) && ok;
    snprintf(name, sizeof(name), "%s.command", label);
    ok = make_path(record_path, sizeof(record_path), directory, name) && ok;
    FILE* record = ok ? fopen(record_path, "wb") : NULL;
    ok = ok && record != NULL;
    if (record)
    {
        for (unsigned i = 0; arguments[i]; ++i) fprintf(record, "argv[%u]=%s\n", i, arguments[i]);
        if (ferror(record)) ok = false;
    }
    int out = ok ? open(out_path, O_WRONLY | O_CREAT | O_EXCL, 0600) : -1;
    int err = ok ? open(err_path, O_WRONLY | O_CREAT | O_EXCL, 0600) : -1;
    ok = ok && out >= 0 && err >= 0;
    pid_t pid = ok ? fork() : -1;
    if (pid == 0)
    {
        if (setpgid(0, 0) || dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0) _exit(125);
        close(out);
        close(err);
        execvp(arguments[0], (char* const*)arguments);
        perror("execvp");
        _exit(125);
    }
    if (out >= 0) close(out);
    if (err >= 0) close(err);
    bool timed_out = false, waited = false;
    int status = -1;
    struct timespec start = {0};
    ok = ok && pid > 0;
    if (ok && clock_gettime(CLOCK_MONOTONIC, &start)) ok = false;
    while (pid > 0 && !waited)
    {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) waited = true;
        else if (result < 0 && errno != EINTR) { ok = false; waited = true; }
        if (!waited)
        {
            struct timespec now = {0};
            if (clock_gettime(CLOCK_MONOTONIC, &now)) ok = false;
            if (!ok || now.tv_sec - start.tv_sec >= 20)
            {
                timed_out = true;
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
                waited = true;
            }
            else
            {
                struct timespec pause = {.tv_sec = 0, .tv_nsec = 10000000};
                nanosleep(&pause, NULL);
            }
        }
    }
    int exit_code = waited && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int signal_number = waited && WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    if (record)
    {
        fprintf(record, "raw_status=%d\nexit=%d\nsignal=%d\ntimeout=%u\n", status, exit_code, signal_number, timed_out);
        if (fclose(record)) ok = false;
    }
    ok = ok && waited && !timed_out && exit_code == 0;
    if (output && output_capacity) ok = read_text(out_path, output, output_capacity) && ok;
    printf("COMMAND dir=%s label=%s exit=%d signal=%d timeout=%u\n", directory, label, exit_code, signal_number, timed_out);
    fflush(stdout);
    return ok;
}

static bool write_layout(FILE* file, Spec s)
{
    for (unsigned i = 0; i < s.noise; ++i) fprintf(file, "struct Unrelated%u { char a[%u]; };\n", i, i + 1);
    if (s.kind == 0)
    {
        if (s.macro)
        {
            fprintf(file, "#define ENTER _Pragma(\"pack(%s%u)\")\n", s.paired ? "push," : "", s.pack);
            fprintf(file, "#define LEAVE _Pragma(\"pack(pop)\")\nENTER\n");
        }
        else fprintf(file, "#pragma pack(%s%u)\n", s.paired ? "push," : "", s.pack);
    }
    fprintf(file, "struct %sPacket { ", s.kind == 1 ? "__attribute__((packed)) " : "");
    if (s.prefix == 1) fprintf(file, "unsigned char lead; ");
    else fprintf(file, "unsigned char lead[%u]; ", s.prefix);
    fprintf(file, "unsigned payload; ");
    if (s.tail) fprintf(file, "unsigned char tail[%u]; ", s.tail);
    fprintf(file, "};\n");
    if (s.kind == 0 && s.paired) fprintf(file, "%s\n", s.macro ? "LEAVE" : "#pragma pack(pop)");
    if (s.natural_sentinel) fprintf(file, "struct Natural { unsigned char lead; unsigned payload; };\n");
    return !ferror(file);
}

static bool write_inputs(char const* directory, Spec s)
{
    char path[1200];
    bool ok = make_path(path, sizeof(path), directory, "source.c");
    FILE* file = ok ? fopen(path, "wb") : NULL;
    ok = ok && file != NULL;
    if (file)
    {
        if (s.header) fprintf(file, "#include \"layout.h\"\n");
        else ok = write_layout(file, s) && ok;
        fprintf(file, "_Static_assert(sizeof(unsigned) == 4, \"requires 32-bit unsigned\");\n");
        fprintf(file, "extern int printf(char const *, ...);\nint main(void)\n{\n");
        fprintf(file, "    printf(\"%%u %%u %%u %%u\\n\", (unsigned)sizeof(struct Packet), (unsigned)_Alignof(struct Packet),\n");
        fprintf(file, "           (unsigned)__builtin_offsetof(struct Packet, payload), %s);\n    return 0;\n}\n",
                s.natural_sentinel ? "(unsigned)sizeof(struct Natural)" : "0u");
        if (ferror(file)) ok = false;
        if (fclose(file)) ok = false;
    }
    if (s.header)
    {
        ok = make_path(path, sizeof(path), directory, "layout.h") && ok;
        file = ok ? fopen(path, "wb") : NULL;
        ok = ok && file != NULL;
        if (file) { ok = write_layout(file, s) && ok; if (fclose(file)) ok = false; }
    }
    if (s.extra)
    {
        ok = make_path(path, sizeof(path), directory, "unrelated.c") && ok;
        file = ok ? fopen(path, "wb") : NULL;
        ok = ok && file != NULL;
        if (file)
        {
            fprintf(file, "unsigned unrelated_translation_unit(void) { return 19; }\n");
            if (ferror(file)) ok = false;
            if (fclose(file)) ok = false;
        }
    }
    return ok;
}

static void expectation(Spec s, bool natural, char* output, size_t capacity)
{
    unsigned alignment = natural || s.kind == 2 ? 4 : s.kind == 1 ? 1 : s.pack;
    unsigned offset = (s.prefix + alignment - 1) / alignment * alignment;
    unsigned size = (offset + 4 + s.tail + alignment - 1) / alignment * alignment;
    snprintf(output, capacity, "%u %u %u %u\n", size, alignment, offset, s.natural_sentinel ? 8u : 0u);
}

static bool compile_file(Spec s, char const* directory, char const* label, unsigned producer,
                         bool preprocess, char const* input, char const* output, bool preprocessed)
{
    char mode[80], jobs[48], other[1200];
    snprintf(mode, sizeof(mode), "-fregister-allocator=%s", allocator);
    snprintf(jobs, sizeof(jobs), "-fcompile-jobs=%u", s.jobs);
    bool ok = make_path(other, sizeof(other), directory, "unrelated.c");
    char const* args[48];
    unsigned n = 0;
    args[n++] = producer == 0 ? ide : producer == 1 ? "clang" : "gcc";
    if (producer == 0) { args[n++] = "cc"; args[n++] = "-target"; args[n++] = "x86_64-unknown-linux"; }
    else if (producer == 1) args[n++] = "--target=x86_64-unknown-linux-gnu";
    else args[n++] = "-m64";
    args[n++] = "-std=c17";
    args[n++] = "-nostdinc";
    args[n++] = "-g0";
    args[n++] = "-O0";
    args[n++] = "-fwrapv";
    args[n++] = "-fno-strict-aliasing";
    args[n++] = "-funsigned-char";
    if (preprocess) args[n++] = "-E";
    else if (producer == 0)
    {
        args[n++] = mode;
        args[n++] = jobs;
        args[n++] = frontend_ssa ? "-ffrontend-ssa" : "-fno-frontend-ssa";
        args[n++] = "-fverify-codegen";
        if (strcmp(allocator, "none")) args[n++] = "-fno-machine-fallback";
    }
    if (preprocessed && s.explicit_phase) { args[n++] = "-x"; args[n++] = "cpp-output"; }
    args[n++] = input;
    if (preprocessed && s.explicit_phase) { args[n++] = "-x"; args[n++] = "none"; }
    if (!preprocess && s.extra) args[n++] = other;
    args[n++] = "-o";
    args[n++] = output;
    args[n] = NULL;
    // Every evaluation owns a new directory. An existing output is an error,
    // never removed and reused as apparent success.
    struct stat existing;
    if (stat(output, &existing) == 0) ok = false;
    if (ok) ok = run_command(directory, label, args, NULL, 0) && nonempty_file(output);
    return ok;
}

static bool compile_run(Spec s, char const* directory, char const* label, unsigned producer,
                        char const* source, bool preprocessed, char* observation, size_t capacity)
{
    char binary[1200], name[128], run_label[128];
    snprintf(name, sizeof(name), "%s.exe", label);
    snprintf(run_label, sizeof(run_label), "%s-run", label);
    bool ok = make_path(binary, sizeof(binary), directory, name);
    if (ok) ok = compile_file(s, directory, label, producer, false, source, binary, preprocessed);
    if (ok)
    {
        char const* args[] = {binary, NULL};
        ok = run_command(directory, run_label, args, observation, capacity);
    }
    return ok;
}

static Observation evaluate(Spec s, char const* purpose)
{
    Observation result = {0};
    char name[128], source[1200], current[1200], label[128], values[128];
    snprintf(name, sizeof(name), "case-%04u", serial++);
    bool valid = (s.pack == 1 || s.pack == 2 || s.pack == 4) && s.prefix >= 1 && s.prefix <= 3 && s.tail <= 2 &&
                 s.noise <= 3 && s.passes >= 1 && s.passes <= 2 && s.jobs >= 1 && s.jobs <= 2 &&
                 (!s.natural_sentinel || s.paired || s.kind != 0) && (s.kind != 1 || s.pack == 1);
    bool ok = valid && make_path(result.directory, sizeof(result.directory), root, name);
    if (ok) ok = mkdir(result.directory, 0700) == 0;
    if (ok) ok = write_inputs(result.directory, s);
    ok = make_path(source, sizeof(source), result.directory, "source.c") && ok;
    expectation(s, false, result.expected, sizeof(result.expected));
    expectation(s, true, result.natural, sizeof(result.natural));
    char spec_path[1200];
    if (make_path(spec_path, sizeof(spec_path), result.directory, "spec.txt"))
    {
        FILE* file = ok ? fopen(spec_path, "wb") : NULL;
        if (file)
        {
            fprintf(file, "purpose=%s\npack=%u prefix=%u tail=%u noise=%u header=%u macro=%u extra=%u passes=%u explicit_phase=%u jobs=%u sentinel=%u paired=%u kind=%u\nallocator=%s frontend_ssa=%u\nexpected=%snatural=%s",
                    purpose, s.pack, s.prefix, s.tail, s.noise, s.header, s.macro, s.extra, s.passes, s.explicit_phase,
                    s.jobs, s.natural_sentinel, s.paired, s.kind, allocator, frontend_ssa, result.expected, result.natural);
            if (ferror(file)) ok = false;
            if (fclose(file)) ok = false;
        }
        else ok = false;
    }
    else ok = false;
    for (unsigned producer = 0; producer < 3 && ok; ++producer)
    {
        snprintf(label, sizeof(label), "direct-%u", producer);
        ok = compile_run(s, result.directory, label, producer, source, false, values, sizeof(values));
        ok = ok && strcmp(values, result.expected) == 0;
    }
    snprintf(current, sizeof(current), "%s", source);
    for (unsigned pass = 0; pass < s.passes && ok; ++pass)
    {
        snprintf(name, sizeof(name), "split-%u.i", pass);
        ok = make_path(result.split, sizeof(result.split), result.directory, name) && ok;
        snprintf(label, sizeof(label), "preprocess-%u", pass);
        if (ok) ok = compile_file(s, result.directory, label, 0, true, current, result.split, pass != 0);
        snprintf(current, sizeof(current), "%s", result.split);
    }
    if (ok) ok = compile_run(s, result.directory, "split-buster", 0, result.split, true, result.actual, sizeof(result.actual));
    result.ready = ok;
    result.equivalent = ok && strcmp(result.actual, result.expected) == 0;
    result.violation = ok && !result.equivalent && strcmp(result.actual, result.natural) == 0;
    if (!ok || (!result.equivalent && !result.violation)) ++setup_errors;
    ++pairs;
    if (result.violation) ++violations;
    printf("PAIR purpose=%s directory=%s ready=%u equivalent=%u packing_loss=%u expected=[%s] actual=[%s]\n",
           purpose, result.directory, result.ready, result.equivalent, result.violation, result.expected, result.actual);
    fflush(stdout);
    return result;
}

static void cross_controls(Spec s, Observation observed)
{
    char source[1200], intermediate[1200], label[128], name[128], values[128];
    bool ok = observed.ready && make_path(source, sizeof(source), observed.directory, "source.c");
    for (unsigned producer = 1; producer <= 2 && ok; ++producer)
    {
        snprintf(name, sizeof(name), "reference-%u.i", producer);
        ok = make_path(intermediate, sizeof(intermediate), observed.directory, name) && ok;
        snprintf(label, sizeof(label), "reference-preprocess-%u", producer);
        if (ok) ok = compile_file(s, observed.directory, label, producer, true, source, intermediate, false);
        snprintf(label, sizeof(label), "reference-self-%u", producer);
        if (ok) ok = compile_run(s, observed.directory, label, producer, intermediate, true, values, sizeof(values));
        ok = ok && strcmp(values, observed.expected) == 0;
        snprintf(label, sizeof(label), "reference-to-buster-%u", producer);
        if (ok) ok = compile_run(s, observed.directory, label, 0, intermediate, true, values, sizeof(values));
        ok = ok && strcmp(values, observed.expected) == 0;
        snprintf(label, sizeof(label), "buster-to-reference-%u", producer);
        if (ok) ok = compile_run(s, observed.directory, label, producer, observed.split, true, values, sizeof(values));
        ok = ok && strcmp(values, observed.actual) == 0;
    }
    if (!ok) ++control_failures;
    printf("CROSS_CONTROLS directory=%s pass=%u\n", observed.directory, ok);
}

static Spec simplify(Spec value, unsigned dimension)
{
    switch (dimension)
    {
    case 0: value.noise = 0; break;
    case 1: value.extra = 0; break;
    case 2: value.jobs = 1; break;
    case 3: value.header = 0; break;
    case 4: value.macro = 0; break;
    case 5: value.passes = 1; break;
    case 6: value.explicit_phase = 0; break;
    case 7: value.tail = 0; break;
    case 8: value.prefix = 1; break;
    case 9: value.pack = 1; break;
    case 10: value.natural_sentinel = 0; break;
    case 11: value.paired = 0; break;
    default: break;
    }
    return value;
}

int main(int argc, char** argv)
{
    int status = 2;
    if (argc == 3 && strlen(argv[1]) < 700 && strlen(argv[2]) < 700)
    {
        ide = argv[1];
        root = argv[2];
        if (mkdir(root, 0700) == 0)
        {
            Spec initial = {.pack = 2, .prefix = 3, .tail = 2, .noise = 3, .header = 1, .macro = 1,
                            .extra = 1, .passes = 2, .explicit_phase = 1, .jobs = 2, .natural_sentinel = 1, .paired = 1};
            Spec minimum = initial;
            Observation first = evaluate(initial, "initial");
            if (first.ready) cross_controls(initial, first);
            if (first.violation)
            {
                for (unsigned dimension = 0; dimension < 12; ++dimension)
                {
                    Spec candidate = simplify(minimum, dimension);
                    Observation a = evaluate(candidate, "reducer-first");
                    Observation b = evaluate(candidate, "reducer-second");
                    replays += 2;
                    bool keep = a.violation && b.violation && strcmp(a.expected, b.expected) == 0 && strcmp(a.actual, b.actual) == 0;
                    printf("REDUCE dimension=%u retained=%u first=%s second=%s\n", dimension, keep, a.directory, b.directory);
                    if (keep) { minimum = candidate; ++accepts; }
                }
            }
            Observation last = evaluate(minimum, "reduced-final");
            if (last.ready) cross_controls(minimum, last);
            // This deliberately violates the law's semantic-state precondition.
            // Both programs are valid; equality MUST be rejected when pack is removed.
            Spec stripped = minimum;
            stripped.kind = 2;
            Observation negative = evaluate(stripped, "invalid-equivalence-remove-pack");
            bool negative_ok = negative.ready && negative.equivalent && last.ready && strcmp(negative.expected, last.expected) != 0;
            if (!negative_ok) ++control_failures;
            printf("NEGATIVE_CONTROL removed_pack_distinguished=%u\n", negative_ok);
            // In-band packed attributes and a nonrestrictive pack(4) are positive controls.
            Spec attribute = minimum;
            attribute.kind = 1;
            attribute.pack = 1;
            Observation in_band = evaluate(attribute, "positive-in-band-attribute");
            if (!in_band.equivalent) ++control_failures;
            Spec natural = minimum;
            natural.pack = 4;
            Observation inactive = evaluate(natural, "positive-nonrestrictive-pack");
            if (!inactive.equivalent) ++control_failures;
            // Hold input order and semantics fixed; vary only supported driver modes.
            char const* modes[] = {"none", "mir-stack", "fast", "quality"};
            for (unsigned mode = 0; mode < 4; ++mode)
            {
                allocator = modes[mode];
                for (unsigned ssa = 0; ssa < 2; ++ssa)
                {
                    frontend_ssa = ssa != 0;
                    for (unsigned jobs = 1; jobs <= 2; ++jobs)
                    {
                        Spec cell = minimum;
                        cell.extra = 1;
                        cell.jobs = jobs;
                        Observation row = evaluate(cell, "native-mode-matrix");
                        if (row.ready && row.violation != last.violation) ++control_failures;
                    }
                }
            }
            printf("MINIMUM directory=%s split=%s\n", last.directory, last.split);
            printf("SUMMARY pairs=%u packing_loss=%u setup_errors=%u control_failures=%u reducer_accepts=%u reducer_replays=%u\n",
                   pairs, violations, setup_errors, control_failures, accepts, replays);
            status = setup_errors || control_failures ? 2 : violations ? 1 : 0;
        }
        else perror("fresh output directory required");
    }
    else fprintf(stderr, "usage: probe ABSOLUTE_IDE NEW_OUTPUT_DIRECTORY\n");
    return status;
}
