// Alternative performance evidence for claims that do not depend on Zen 5 (#4013).
// Ownership: the `work-count` and `native-host` evidence classes of
// docs/performance-validation-v1.json. This tool never produces Zen 5 evidence:
// every summary it writes carries PE_NOT_ZEN5, a `uarch` or `zen5` claim is
// refused with the physical route it needs, and performance_inventory_test.py
// refuses this tool as the consumer of a covered Zen 5 row.
// Entry points:
//   classify CLAIM                         evidence class, tools and route for a claim
//   preflight --tool T [-- COMMAND ...]    capability discovery with explicit reasons
//   compare --claim C --tool T --baseline BIN --candidate BIN --out DIR [options] -- ARGS ...
//   --self-test                            synthetic parser, verdict and refusal checks
// compare runs both arms in this environment in alternating order (pair i runs
// baseline first when i is even), retains every raw output under DIR, and writes
// DIR/summary.json (schema buster-performance-evidence-v1). Instrumented counts
// (callgrind, cachegrind) need repeatable arms and a separated, minimum-effect
// change; uninstrumented observations (time, perf-stat) use an exact two-sided
// sign test over paired differences plus the minimum effect. A missing tool,
// an ISA the profiler cannot execute, a denied or unsupported counter, too
// little counter running time, an unexpected exit or a missing required metric
// leaves the comparison incomplete (exit 2) and never becomes a zero value.
// Map: pe_claims, pe_sha256_file, pe_which, pe_run, pe_parse_profile,
//      pe_perf_event, pe_perf_events_valid, pe_parse_perf, pe_valgrind_problem, pe_perf_problem, pe_verdicts,
//      pe_write_summary, pe_finish, pe_path_join, pe_remove_tree, pe_preflight, pe_compare, pe_classify, pe_self_test, main.
#include "ci_metrics_json.h"
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#define PE_SCHEMA "buster-performance-evidence-v1"
#define PE_NOT_ZEN5 "not Zen 5 evidence: never discharges a zen5 inventory row or a Zen 5 claim (#4013)"
#define PE_RUNS_MAX 64u
#define PE_METRICS 24u
#define PE_ARGS 256u
#define PE_PATH 4096u
#define PE_TEXT 4096u
#define PE_MODEL_LINES 8u
#define PE_PROFILE_BYTES (512u * 1024u * 1024u)
#define PE_LOG_BYTES (16u * 1024u * 1024u)
// Instrumented counts must repeat within 0.1% inside one arm.
#define PE_REPEAT_TOLERANCE 0.001
// perf scales a multiplexed counter; below half its running time it is refused.
#define PE_MIN_RUNNING_PERCENT 50.0
#define PE_SIGNIFICANCE 0.05
#define PE_DEFAULT_EFFECT 0.01
#define PE_DEFAULT_TIMEOUT 3600.0
#define PE_DEFAULT_EVENTS "task-clock,instructions,cycles,context-switches,page-faults"

typedef enum PeTool
{
    PE_TOOL_NONE,
    PE_TOOL_CALLGRIND,
    PE_TOOL_CACHEGRIND,
    PE_TOOL_PERF_STAT,
    PE_TOOL_TIME,
    PE_TOOL_COUNT,
} PeTool;

typedef enum PeKind
{
    PE_KIND_COUNT,
    PE_KIND_TIMED,
} PeKind;

typedef struct PeClaim PeClaim;
struct PeClaim
{
    const char *name, *evidence_class, *tools, *route, *primary, *establishes, *does_not_establish;
};

// The claim table; performance_inventory_test.py checks it against the
// inventory's `claims` object, so the two cannot drift apart.
BUSTER_GLOBAL_LOCAL const PeClaim pe_claims[] = {
    {"work-reduction", "work-count", "callgrind,cachegrind,perf-stat", "hosted",
     "Ir (callgrind/cachegrind) or instructions (perf-stat)",
     "less executed work (instructions, references, operations) for the measured workload and inputs",
     "lower elapsed time; hardware-independent constants; Zen 5 behavior"},
    {"native-runtime", "native-host", "time,perf-stat", "hosted",
     "wall_seconds (time) or task-clock (perf-stat)",
     "an uninstrumented paired wall/CPU/resource change for the stated workload on the observed host and OS",
     "transfer to Zen 5 or to another CPU, OS, filesystem or concurrency level"},
    {"uarch", "zen5-native", "", "pull-compare or direct-workload on the approved Ryzen 7 9700X", "",
     "SIMD, branched/branchless, scheduling, alignment, cache/TLB tuning on the physical target",
     "nothing measured elsewhere: simulated caches, machine models, other CPUs and -march=znver5 are not Zen 5"},
    {"zen5", "zen5-native", "", "pull-compare or direct-workload on the approved Ryzen 7 9700X", "",
     "an explicit Zen 5 speed claim on the physical target", "nothing measured elsewhere"},
};

BUSTER_GLOBAL_LOCAL const char *const pe_tool_names[PE_TOOL_COUNT] = {"", "callgrind", "cachegrind", "perf-stat", "time"};

typedef struct PeMetric PeMetric;
struct PeMetric
{
    char name[48], unit[16], verdict[24], missing[192], scope[16];
    PeKind kind;
    int required, multiplexed;
    unsigned count[2];
    double value[2][PE_RUNS_MAX];
    double change, p_value;
};

typedef struct PeReport PeReport;
struct PeReport
{
    const PeClaim *claim;
    PeTool tool;
    unsigned runs, warmup, metric_count, model_count, processes[2][PE_RUNS_MAX];
    double effect;
    int complete, a_a, cache_sim, branch_sim;
    char problems[PE_TEXT], tool_version[128], result[256];
    char model[PE_MODEL_LINES][128];
    PeMetric metrics[PE_METRICS];
};

typedef struct PeRun PeRun;
struct PeRun
{
    int started, exited, status, signal, timed_out;
    double wall;
    struct rusage usage;
};

BUSTER_GLOBAL_LOCAL void pe_problem(PeReport *r, const char *a, const char *b)
{
    size_t used = strlen(r->problems);
    if (used + 4 < sizeof(r->problems))
        snprintf(r->problems + used, sizeof(r->problems) - used, "%s%s%s", used ? "; " : "", a, b ? b : "");
    r->complete = 0;
}

BUSTER_GLOBAL_LOCAL const PeClaim *pe_claim(const char *name)
{
    const PeClaim *result = NULL;
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(pe_claims) && !result; ++i)
        if (cm_equal(pe_claims[i].name, name)) result = &pe_claims[i];
    return result;
}

BUSTER_GLOBAL_LOCAL PeTool pe_tool(const char *name)
{
    PeTool result = PE_TOOL_NONE;
    for (unsigned i = 1; i < PE_TOOL_COUNT && result == PE_TOOL_NONE; ++i)
        if (cm_equal(pe_tool_names[i], name)) result = (PeTool)i;
    return result;
}

// Whether `tool` appears in a claim's comma-separated tool list.
BUSTER_GLOBAL_LOCAL int pe_claim_allows(const PeClaim *claim, PeTool tool)
{
    int result = 0;
    const char *list = claim ? claim->tools : "";
    size_t n = strlen(pe_tool_names[tool]);
    for (const char *at = list; tool != PE_TOOL_NONE && *at && !result; )
    {
        const char *end = strchr(at, ',');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        result = length == n && strncmp(at, pe_tool_names[tool], n) == 0;
        at += length + (end != NULL);
    }
    return result;
}

// SHA-256 (FIPS 180-4) for binary identities.
typedef struct PeSha PeSha;
struct PeSha { uint32_t h[8]; uint8_t block[64]; uint64_t bytes; unsigned fill; };

BUSTER_GLOBAL_LOCAL const uint32_t pe_sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

BUSTER_GLOBAL_LOCAL uint32_t pe_rotate(uint32_t x, unsigned n)
{
    uint32_t result = (x >> n) | (x << (32u - n));
    return result;
}

BUSTER_GLOBAL_LOCAL void pe_sha_block(PeSha *s)
{
    uint32_t w[64], v[8];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (uint32_t)s->block[i * 4] << 24 | (uint32_t)s->block[i * 4 + 1] << 16 |
            (uint32_t)s->block[i * 4 + 2] << 8 | (uint32_t)s->block[i * 4 + 3];
    for (unsigned i = 16; i < 64; ++i)
        w[i] = w[i - 16] + (pe_rotate(w[i - 15], 7) ^ pe_rotate(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
            (pe_rotate(w[i - 2], 17) ^ pe_rotate(w[i - 2], 19) ^ (w[i - 2] >> 10));
    memcpy(v, s->h, sizeof(v));
    for (unsigned i = 0; i < 64; ++i)
    {
        uint32_t t1 = v[7] + (pe_rotate(v[4], 6) ^ pe_rotate(v[4], 11) ^ pe_rotate(v[4], 25)) +
            ((v[4] & v[5]) ^ (~v[4] & v[6])) + pe_sha_k[i] + w[i];
        uint32_t t2 = (pe_rotate(v[0], 2) ^ pe_rotate(v[0], 13) ^ pe_rotate(v[0], 22)) +
            ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        memmove(v + 1, v, 7 * sizeof(uint32_t));
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (unsigned i = 0; i < 8; ++i) s->h[i] += v[i];
}

BUSTER_GLOBAL_LOCAL void pe_sha_update(PeSha *s, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        s->block[s->fill++] = data[i];
        if (s->fill == 64) { pe_sha_block(s); s->fill = 0; }
    }
    s->bytes += n;
}

BUSTER_GLOBAL_LOCAL void pe_sha_hex(const uint8_t *data, size_t n, FILE *file, char out[65])
{
    PeSha s = {{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, {0}, 0, 0};
    uint8_t buffer[65536], pad = 0x80, zero = 0, length[8];
    if (data) pe_sha_update(&s, data, n);
    for (size_t got = file ? fread(buffer, 1, sizeof(buffer), file) : 0; got; got = fread(buffer, 1, sizeof(buffer), file))
        pe_sha_update(&s, buffer, got);
    uint64_t bits = s.bytes * 8;
    for (unsigned i = 0; i < 8; ++i) length[i] = (uint8_t)(bits >> (56 - 8 * i));
    pe_sha_update(&s, &pad, 1);
    while (s.fill != 56) pe_sha_update(&s, &zero, 1);
    pe_sha_update(&s, length, 8);
    for (unsigned i = 0; i < 8; ++i) snprintf(out + i * 8, 9, "%08x", s.h[i]);
}

BUSTER_GLOBAL_LOCAL int pe_sha256_file(const char *path, char out[65])
{
    FILE *file = fopen(path, "rb");
    int result = file != NULL;
    if (file) { pe_sha_hex(NULL, 0, file, out); result = !ferror(file); fclose(file); }
    if (!result) out[0] = 0;
    return result;
}

// Resolve an executable through PATH (or a path containing '/').
BUSTER_GLOBAL_LOCAL int pe_which(const char *name, char *out, size_t capacity)
{
    int result = 0;
    if (strchr(name, '/')) { result = access(name, X_OK) == 0; if (result) cm_copy(out, capacity, name); }
    const char *path = strchr(name, '/') ? NULL : getenv("PATH");
    for (const char *at = path; at && *at && !result; )
    {
        const char *end = strchr(at, ':');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (length && length + strlen(name) + 2 < capacity)
        {
            snprintf(out, capacity, "%.*s/%s", (int)length, at, name);
            result = access(out, X_OK) == 0;
        }
        at += length + (end != NULL);
    }
    if (!result && capacity) out[0] = 0;
    return result;
}

BUSTER_GLOBAL_LOCAL double pe_clock(void)
{
    struct timespec t = {0};
    clock_gettime(CLOCK_MONOTONIC, &t);
    double result = (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
    return result;
}

// Run argv with stdout/stderr redirected to files; wait4 includes reaped descendants in rusage.
BUSTER_GLOBAL_LOCAL PeRun pe_run(char *const *argv, const char *out_path, const char *err_path, double timeout)
{
    PeRun result = {0};
    int out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644), err = open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    double start = pe_clock();
    pid_t pid = out >= 0 && err >= 0 ? fork() : -1;
    if (pid == 0)
    {
        setpgid(0, 0);
        dup2(out, STDOUT_FILENO);
        dup2(err, STDERR_FILENO);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (pid > 0)
    {
        int status = 0, done = 0;
        setpgid(pid, pid);
        result.started = 1;
        while (!done)
        {
            pid_t waited = wait4(pid, &status, WNOHANG, &result.usage);
            done = waited == pid || (waited < 0 && errno != EINTR);
            if (!done && pe_clock() - start > timeout)
            {
                kill(-pid, SIGKILL);
                while (wait4(pid, &status, 0, &result.usage) < 0 && errno == EINTR) { }
                result.timed_out = done = 1;
            }
            if (!done) { struct timespec pause = {0, 1000000}; nanosleep(&pause, NULL); }
        }
        result.wall = pe_clock() - start;
        result.exited = WIFEXITED(status);
        result.status = result.exited ? WEXITSTATUS(status) : -1;
        result.signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    }
    if (out >= 0) close(out);
    if (err >= 0) close(err);
    return result;
}

// Run a short probe and return its combined stdout+stderr text (caller frees).
BUSTER_GLOBAL_LOCAL char *pe_capture(char *const *argv, const char *directory, const char *label, PeRun *run)
{
    char out[PE_PATH], err[PE_PATH];
    size_t n = 0, m = 0;
    snprintf(out, sizeof(out), "%s/%s.stdout", directory, label);
    snprintf(err, sizeof(err), "%s/%s.stderr", directory, label);
    *run = pe_run(argv, out, err, 120.0);
    char *a = cm_read(out, PE_LOG_BYTES, &n), *b = cm_read(err, PE_LOG_BYTES, &m);
    char *result = a && b ? malloc(n + m + 2) : NULL;
    if (result) { memcpy(result, a, n); result[n] = '\n'; memcpy(result + n + 1, b, m); result[n + m + 1] = 0; }
    free(a);
    free(b);
    return result;
}

BUSTER_GLOBAL_LOCAL PeMetric *pe_metric(PeReport *r, const char *name, const char *unit, PeKind kind, int required)
{
    PeMetric *result = NULL;
    for (unsigned i = 0; i < r->metric_count && !result; ++i)
        if (cm_equal(r->metrics[i].name, name)) result = &r->metrics[i];
    if (!result && r->metric_count < PE_METRICS)
    {
        result = &r->metrics[r->metric_count++];
        memset(result, 0, sizeof(*result));
        cm_copy(result->name, sizeof(result->name), name);
        cm_copy(result->unit, sizeof(result->unit), unit);
        result->kind = kind;
        result->required = required;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void pe_record(PeReport *r, unsigned arm, const char *name, const char *unit, PeKind kind, int required, double value)
{
    PeMetric *m = pe_metric(r, name, unit, kind, required);
    if (m && m->count[arm] < PE_RUNS_MAX) m->value[arm][m->count[arm]++] = value;
}

BUSTER_GLOBAL_LOCAL void pe_missing(PeReport *r, const char *name, const char *unit, PeKind kind, int required, const char *reason)
{
    PeMetric *m = pe_metric(r, name, unit, kind, required);
    if (m && !m->missing[0]) cm_copy(m->missing, sizeof(m->missing), reason);
}

// Parse a callgrind or cachegrind profile: the `events:` names and the
// `totals:` (callgrind) or `summary:` line values. Returns the event count.
BUSTER_GLOBAL_LOCAL unsigned pe_parse_profile(const char *text, char names[][48], double *values, unsigned capacity,
    char model[][128], unsigned *model_count)
{
    unsigned names_count = 0, values_count = 0;
    int have_totals = 0;
    for (const char *line = text; line && *line; )
    {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        int events = length > 7 && strncmp(line, "events:", 7) == 0;
        int totals = length > 7 && strncmp(line, "totals:", 7) == 0;
        int summary = length > 8 && strncmp(line, "summary:", 8) == 0 && !have_totals;
        if (length > 5 && strncmp(line, "desc:", 5) == 0 && model && *model_count < PE_MODEL_LINES)
        {
            snprintf(model[*model_count], 128, "%.*s", (int)(length - 5 < 127 ? length - 5 : 127), line + 5);
            cm_normalize(model[*model_count], 128, model[*model_count]);
            ++*model_count;
        }
        if (events || totals || summary)
        {
            unsigned count = 0;
            const char *at = line + (summary ? 8 : 7), *stop = line + length;
            while (at < stop && count < capacity)
            {
                while (at < stop && (*at == ' ' || *at == '\t' || *at == '\r')) ++at;
                const char *word = at;
                while (at < stop && *at != ' ' && *at != '\t' && *at != '\r') ++at;
                if (at > word && events) snprintf(names[count++], 48, "%.*s", (int)(at - word < 47 ? at - word : 47), word);
                else if (at > word)
                {
                    char number[32];
                    snprintf(number, sizeof(number), "%.*s", (int)(at - word < 31 ? at - word : 31), word);
                    uint64_t parsed = 0;
                    values[count++] = cm_unsigned(number, &parsed) ? (double)parsed : -1.0;
                }
            }
            if (events) names_count = count;
            else { values_count = count; have_totals |= totals; }
        }
        line = end ? end + 1 : NULL;
    }
    unsigned result = names_count && names_count == values_count ? names_count : 0;
    for (unsigned i = 0; i < result; ++i) if (values[i] < 0) result = 0;
    return result;
}

// This receipt supports unqualified event names and the single :u/:k scope
// modifiers. Other perf syntax needs its own scope/identity contract.
BUSTER_GLOBAL_LOCAL int pe_perf_event(const char *event, char base[48], char scope[16])
{
    const char *modifier = strchr(event, ':');
    size_t length = modifier ? (size_t)(modifier - event) : strlen(event);
    int valid = length > 0 && strlen(event) < 48 &&
        (!modifier || cm_equal(modifier, ":u") || cm_equal(modifier, ":k"));
    for (size_t i = 0; valid && i < length; ++i)
        valid = (event[i] >= 'a' && event[i] <= 'z') || (event[i] >= 'A' && event[i] <= 'Z') ||
            (event[i] >= '0' && event[i] <= '9') || event[i] == '-' || event[i] == '_' || event[i] == '.';
    snprintf(base, 48, "%.*s", (int)(length < 47 ? length : 47), event);
    cm_copy(scope, 16, modifier ? modifier + 1 : "unqualified");
    return valid;
}

BUSTER_GLOBAL_LOCAL int pe_perf_events_valid(const char *events)
{
    int valid = events[0] != 0;
    for (const char *at = events; valid && *at; )
    {
        const char *end = strchr(at, ',');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        char event[48], base[48], scope[16];
        snprintf(event, sizeof(event), "%.*s", (int)(length < 47 ? length : 47), at);
        valid = length < sizeof(event) && pe_perf_event(event, base, scope) && (!end || end[1]);
        at += length + (end != NULL);
    }
    return valid;
}

// Parse `perf stat -x ,` output for the requested events; absent, unsupported,
// uncounted, conflicting-scope or under-run events stay missing with the reason.
BUSTER_GLOBAL_LOCAL void pe_parse_perf(PeReport *r, unsigned arm, const char *text, const char *events)
{
    for (const char *at = events; *at; )
    {
        const char *end = strchr(at, ',');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        char event[48], base[48], requested_scope[16], observed_scope[16] = "";
        char reason[192] = "event absent from perf output";
        double value = 0;
        int found = 0, multiplexed = 0, conflict = 0;
        snprintf(event, sizeof(event), "%.*s", (int)(length < 47 ? length : 47), at);
        int requested_valid = pe_perf_event(event, base, requested_scope) && length < sizeof(event);
        const char *unit = cm_equal(base, "task-clock") ? "msec" : "count";
        PeKind kind = PE_KIND_TIMED;
        for (const char *line = text; requested_valid && line && *line; )
        {
            const char *stop = strchr(line, '\n');
            size_t n = stop ? (size_t)(stop - line) : strlen(line);
            char row[512], *field[8] = {0};
            unsigned fields = 0;
            snprintf(row, sizeof(row), "%.*s", (int)(n < 511 ? n : 511), line);
            for (char *cursor = row; row[0] != '#' && fields < 8 && cursor; )
            {
                field[fields++] = cursor;
                cursor = strchr(cursor, ',');
                if (cursor) *cursor++ = 0;
            }
            char line_base[48], line_scope[16];
            int observed_valid = pe_perf_event(fields >= 3 ? field[2] : "", line_base, line_scope);
            if (fields >= 3 && cm_equal(line_base, base))
            {
                int scope_matches = observed_valid && (cm_equal(requested_scope, "unqualified") ||
                    cm_equal(requested_scope, line_scope));
                if (!scope_matches && !found)
                    cm_copy(reason, sizeof(reason), "perf event scope does not match requested scope or is unsupported");
                if (scope_matches)
                {
                    conflict |= found;
                    found = 1;
                    cm_copy(observed_scope, sizeof(observed_scope), line_scope);
                    double percent = fields >= 5 && field[4][0] ? strtod(field[4], NULL) : 100.0;
                    if (field[0][0] == '<') snprintf(reason, sizeof(reason), "perf reported %s", field[0]);
                    else
                    {
                        char *tail = NULL;
                        value = strtod(field[0], &tail);
                        int running_ok = tail != field[0] && percent >= PE_MIN_RUNNING_PERCENT;
                        multiplexed = percent < 100.0;
                        if (!running_ok)
                            snprintf(reason, sizeof(reason), "insufficient counter running time %.2f%% < %.0f%%", percent,
                                PE_MIN_RUNNING_PERCENT);
                        else reason[0] = 0;
                    }
                }
            }
            line = stop ? stop + 1 : NULL;
        }
        if (!requested_valid) cm_copy(reason, sizeof(reason), "unsupported perf event syntax: use an event name with optional :u or :k");
        if (conflict) cm_copy(reason, sizeof(reason), "multiple perf counters match the requested event and scope");
        PeMetric *m = pe_metric(r, event, unit, kind, 1);
        if (m && found && !reason[0] && m->scope[0] && !cm_equal(m->scope, observed_scope))
            cm_copy(reason, sizeof(reason), "perf event scope changed between observations");
        if (found && !reason[0])
        {
            pe_record(r, arm, event, unit, kind, 1, value);
            if (m)
            {
                m->multiplexed |= multiplexed;
                cm_copy(m->scope, sizeof(m->scope), observed_scope);
            }
        }
        else pe_missing(r, event, unit, kind, 1, reason);
        at += length + (end != NULL);
    }
}

// An explicit reason when valgrind could not execute the subject, else NULL.
BUSTER_GLOBAL_LOCAL const char *pe_valgrind_problem(const char *log, const PeRun *run)
{
    const char *result = NULL;
    if (log && (strstr(log, "unhandled instruction bytes") || strstr(log, "signal 4 (SIGILL)")))
        result = "valgrind cannot execute this binary's instruction set; build both arms with a declared "
            "profiling-compatible ISA (for example tools/work_ledger/build_subject.sh, -march=x86-64-v3) "
            "and keep that identity separate from native results";
    else if (run && run->exited && run->status == 127 && log && !strstr(log, "valgrind"))
        result = "the tool or subject could not be executed";
    return result;
}

// An explicit reason when perf could not count, else NULL.
BUSTER_GLOBAL_LOCAL const char *pe_perf_problem(const char *log)
{
    const char *result = NULL;
    if (log && (strstr(log, "Access to performance monitoring") || strstr(log, "No permission") ||
            strstr(log, "perf_event_paranoid") || strstr(log, "Permission denied")))
        result = "perf events denied by the kernel (perf_event_paranoid or capabilities); "
            "do not weaken host security, use the time tool or a host that permits counting";
    else if (log && strstr(log, "not supported"))
        result = "a requested perf event is not supported on this host";
    return result;
}

BUSTER_GLOBAL_LOCAL double pe_median(const double *values, unsigned n)
{
    double sorted[PE_RUNS_MAX];
    memcpy(sorted, values, n * sizeof(double));
    for (unsigned i = 1; i < n; ++i)
    {
        double key = sorted[i];
        unsigned j = i;
        while (j > 0 && sorted[j - 1] > key) { sorted[j] = sorted[j - 1]; --j; }
        sorted[j] = key;
    }
    double result = n == 0 ? 0 : n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
    return result;
}

// Exact two-sided sign-test p-value for `smaller` of `n` untied pairs.
BUSTER_GLOBAL_LOCAL double pe_sign_test(unsigned smaller, unsigned n)
{
    double tail = 0, term = 1;
    for (unsigned i = 0; i < n; ++i) term *= 0.5;
    for (unsigned k = 0; k <= smaller && k <= n; ++k)
    {
        tail += term;
        term = term * (double)(n - k) / (double)(k + 1);
    }
    double result = n == 0 ? 1.0 : tail * 2 > 1.0 ? 1.0 : tail * 2;
    return result;
}

BUSTER_GLOBAL_LOCAL void pe_verdicts(PeReport *r)
{
    for (unsigned i = 0; i < r->metric_count; ++i)
    {
        PeMetric *m = &r->metrics[i];
        unsigned n = m->count[0] < m->count[1] ? m->count[0] : m->count[1];
        m->p_value = 1.0;
        if (m->missing[0] || m->count[0] != r->runs || m->count[1] != r->runs)
        {
            cm_copy(m->verdict, sizeof(m->verdict), "missing");
            if (!m->missing[0]) cm_copy(m->missing, sizeof(m->missing), "not observed in every run");
            if (m->required) pe_problem(r, "required metric missing: ", m->name);
        }
        else
        {
            double mb = pe_median(m->value[0], n), mc = pe_median(m->value[1], n);
            double lo[2] = {m->value[0][0], m->value[1][0]}, hi[2] = {lo[0], lo[1]};
            for (unsigned arm = 0; arm < 2; ++arm)
                for (unsigned k = 0; k < n; ++k)
                {
                    lo[arm] = m->value[arm][k] < lo[arm] ? m->value[arm][k] : lo[arm];
                    hi[arm] = m->value[arm][k] > hi[arm] ? m->value[arm][k] : hi[arm];
                }
            m->change = mb > 0 ? mc / mb - 1.0 : mc > 0 ? 1.0 : 0.0;
            const char *verdict = "within-effect";
            if (m->kind == PE_KIND_COUNT)
            {
                int repeatable = (hi[0] - lo[0]) <= PE_REPEAT_TOLERANCE * mb && (hi[1] - lo[1]) <= PE_REPEAT_TOLERANCE * mc;
                if (!repeatable) verdict = "nonrepeatable";
                else if (hi[1] < lo[0] && m->change <= -r->effect) verdict = "improvement";
                else if (lo[1] > hi[0] && m->change >= r->effect) verdict = "regression";
            }
            else
            {
                unsigned below = 0, above = 0;
                for (unsigned k = 0; k < n; ++k)
                {
                    below += m->value[1][k] < m->value[0][k];
                    above += m->value[1][k] > m->value[0][k];
                }
                m->p_value = pe_sign_test(below < above ? below : above, below + above);
                if (m->p_value <= PE_SIGNIFICANCE && m->change <= -r->effect && below > above) verdict = "improvement";
                else if (m->p_value <= PE_SIGNIFICANCE && m->change >= r->effect && above > below) verdict = "regression";
                else if (below + above > 0 && (m->p_value > PE_SIGNIFICANCE || fabs(m->change) >= r->effect)) verdict = "inconclusive";
            }
            cm_copy(m->verdict, sizeof(m->verdict), verdict);
        }
    }
}

// The primary metric's outcome phrased as exactly what it establishes.
BUSTER_GLOBAL_LOCAL void pe_result(PeReport *r)
{
    const char *primary = r->tool == PE_TOOL_TIME ? "wall_seconds" :
        r->tool == PE_TOOL_PERF_STAT ? (cm_equal(r->claim->name, "work-reduction") ? "instructions" : "task-clock") : "Ir";
    const PeMetric *m = NULL;
    unsigned primary_count = 0;
    for (unsigned i = 0; i < r->metric_count; ++i)
    {
        char base[48], scope[16];
        int scoped_primary = r->tool == PE_TOOL_PERF_STAT && pe_perf_event(r->metrics[i].name, base, scope) && cm_equal(base, primary);
        if (cm_equal(r->metrics[i].name, primary) || scoped_primary)
        {
            ++primary_count;
            m = &r->metrics[i];
        }
    }
    if (!primary_count) pe_problem(r, "missing required primary metric: ", primary);
    else if (primary_count > 1) pe_problem(r, "multiple primary perf scopes: select one scope for ", primary);
    if (m) primary = m->name;
    int work = cm_equal(r->claim->name, "work-reduction");
    const char *verdict = m ? m->verdict : "missing";
    if (!r->complete || !m) snprintf(r->result, sizeof(r->result), "incomplete: %s not established", work ? "work change" : "native change");
    else if (r->a_a) snprintf(r->result, sizeof(r->result), "A/A noise control on %s: %s (%+.3f%%)", primary, verdict, m->change * 100);
    else if (cm_equal(verdict, "improvement"))
        snprintf(r->result, sizeof(r->result), work ? "work reduction validated (%s %+.3f%%); native runtime not measured" :
            "native runtime improvement validated on this host and workload (%s %+.3f%%); not transferable to Zen 5",
            primary, m->change * 100);
    else snprintf(r->result, sizeof(r->result), "%s on %s (%+.3f%%): no %s claim established", verdict, primary, m->change * 100,
        work ? "work-reduction" : "native-runtime");
}

// Read a file whose size is not reported (procfs) up to `limit` bytes; caller frees.
BUSTER_GLOBAL_LOCAL char *pe_read_stream(const char *path, size_t limit)
{
    FILE *file = fopen(path, "rb");
    char *result = file ? malloc(limit + 1) : NULL;
    size_t n = 0;
    for (size_t got = result ? fread(result, 1, limit, file) : 0; got && n < limit; got = fread(result + n, 1, limit - n, file))
        n += got;
    if (result) result[n] = 0;
    if (file) fclose(file);
    return result;
}

BUSTER_GLOBAL_LOCAL void pe_host(FILE *f)
{
    struct utsname u = {0};
    char cpu[160] = "unknown";
    char *info = pe_read_stream("/proc/cpuinfo", 1u << 20);
    const char *model = info ? strstr(info, "model name") : NULL;
    const char *colon = model ? strchr(model, ':') : NULL;
    if (colon)
    {
        const char *end = strchr(colon, '\n');
        snprintf(cpu, sizeof(cpu), "%.*s", (int)(end ? end - colon - 1 : 0), colon + 1);
        cm_normalize(cpu, sizeof(cpu), cpu);
    }
    uname(&u);
    fputs("  \"host\": {\"cpu_model\": ", f); cm_quote(f, cpu);
    fputs(", \"os\": ", f); cm_quote(f, u.sysname);
    fputs(", \"release\": ", f); cm_quote(f, u.release);
    fputs(", \"machine\": ", f); cm_quote(f, u.machine);
    fprintf(f, ", \"logical_cpus\": %ld},\n", sysconf(_SC_NPROCESSORS_ONLN));
    free(info);
}

BUSTER_GLOBAL_LOCAL void pe_write_summary(FILE *f, const PeReport *r, const char *const identity[6], char *const *args, unsigned arg_count)
{
    fprintf(f, "{\n  \"schema\": \"%s\",\n  \"claim\": ", PE_SCHEMA); cm_quote(f, r->claim->name);
    fputs(",\n  \"evidence_class\": ", f); cm_quote(f, r->claim->evidence_class);
    fputs(",\n  \"zen5\": ", f); cm_quote(f, PE_NOT_ZEN5);
    fputs(",\n  \"tool\": ", f); cm_quote(f, pe_tool_names[r->tool]);
    fputs(",\n  \"tool_version\": ", f); cm_quote(f, r->tool_version);
    fprintf(f, ",\n  \"complete\": %s,\n  \"a_a_control\": %s,\n  \"result\": ", r->complete ? "true" : "false", r->a_a ? "true" : "false");
    cm_quote(f, r->result);
    fputs(",\n  \"establishes\": ", f); cm_quote(f, r->claim->establishes);
    fputs(",\n  \"does_not_establish\": ", f); cm_quote(f, r->claim->does_not_establish);
    fputs(",\n  \"problems\": ", f); cm_quote(f, r->problems);
    fputs(",\n", f);
    pe_host(f);
    const char *keys[6] = {"path", "sha256", "revision"};
    for (unsigned arm = 0; arm < 2; ++arm)
    {
        fprintf(f, "  \"%s\": {", arm ? "candidate" : "baseline");
        for (unsigned k = 0; k < 3; ++k) { fprintf(f, "%s\"%s\": ", k ? ", " : "", keys[k]); cm_quote(f, identity[arm * 3 + k]); }
        fputs(", \"processes\": [", f);
        for (unsigned k = 0; k < r->runs; ++k) fprintf(f, "%s%u", k ? ", " : "", r->processes[arm][k]);
        fputs("]},\n", f);
    }
    fputs("  \"arguments\": [", f);
    for (unsigned k = 0; k < arg_count; ++k) { if (k) fputs(", ", f); cm_quote(f, args[k]); }
    fprintf(f, "],\n  \"runs\": %u,\n  \"warmup\": %u,\n  \"order\": \"alternating, baseline first on even pairs\",\n"
        "  \"minimum_effect\": %.6f,\n  \"cache_sim\": %s,\n  \"branch_sim\": %s,\n  \"model\": [",
        r->runs, r->warmup, r->effect, r->cache_sim ? "true" : "false", r->branch_sim ? "true" : "false");
    for (unsigned k = 0; k < r->model_count; ++k) { if (k) fputs(", ", f); cm_quote(f, r->model[k]); }
    fputs("],\n  \"metrics\": [", f);
    for (unsigned i = 0; i < r->metric_count; ++i)
    {
        const PeMetric *m = &r->metrics[i];
        fprintf(f, "%s\n    {\"name\": ", i ? "," : ""); cm_quote(f, m->name);
        fputs(", \"unit\": ", f); cm_quote(f, m->unit);
        if (r->tool == PE_TOOL_PERF_STAT) { fputs(", \"scope\": ", f); cm_quote(f, m->scope); }
        fprintf(f, ", \"kind\": \"%s\", \"required\": %s, \"multiplexed\": %s, \"verdict\": ",
            m->kind == PE_KIND_COUNT ? "instrumented-count" : "paired-observation", m->required ? "true" : "false",
            m->multiplexed ? "true" : "false");
        cm_quote(f, m->verdict);
        fputs(", \"missing\": ", f); cm_quote(f, m->missing);
        fprintf(f, ", \"relative_change\": %.9f, \"sign_test_p\": ", m->change);
        if (m->kind == PE_KIND_TIMED) fprintf(f, "%.6g", m->p_value);
        else fputs("null", f);
        for (unsigned arm = 0; arm < 2; ++arm)
        {
            fprintf(f, ", \"%s\": [", arm ? "candidate" : "baseline");
            for (unsigned k = 0; k < m->count[arm]; ++k) fprintf(f, "%s%.17g", k ? ", " : "", m->value[arm][k]);
            fputc(']', f);
        }
        fputc('}', f);
    }
    fputs("\n  ]\n}\n", f);
}

BUSTER_GLOBAL_LOCAL void pe_print(FILE *f, const PeReport *r)
{
    fprintf(f, "PERFORMANCE_EVIDENCE claim=%s class=%s tool=%s complete=%s\n", r->claim->name, r->claim->evidence_class,
        pe_tool_names[r->tool], r->complete ? "yes" : "no");
    fprintf(f, "| metric | baseline median | candidate median | change | verdict |\n|---|---|---|---|---|\n");
    for (unsigned i = 0; i < r->metric_count; ++i)
    {
        const PeMetric *m = &r->metrics[i];
        fprintf(f, "| %s (%s) | %.6g | %.6g | %+.3f%% | %s%s%s |\n", m->name, m->unit, pe_median(m->value[0], m->count[0]),
            pe_median(m->value[1], m->count[1]), m->change * 100, m->verdict, m->missing[0] ? ": " : "", m->missing);
    }
    fprintf(f, "Result: %s\n%s\n", r->result, PE_NOT_ZEN5);
    if (r->problems[0]) fprintf(f, "Problems: %s\n", r->problems);
}

// Finalize the same completeness, receipt and exit contract for CLI and offline controls.
BUSTER_GLOBAL_LOCAL int pe_finish(PeReport *r, FILE *summary, const char *const identity[6], char *const *args, unsigned arg_count)
{
    pe_verdicts(r);
    pe_result(r);
    if (summary) pe_write_summary(summary, r, identity, args, arg_count);
    pe_print(stdout, r);
    int result = r->complete ? 0 : 2;
    return result;
}

// Sum every per-process profile `<prefix>.<pid>.<suffix>` for one run.
BUSTER_GLOBAL_LOCAL void pe_collect_profiles(PeReport *r, unsigned arm, unsigned run, const char *directory, const char *prefix,
    const char *suffix)
{
    char names[PE_METRICS][48], path[PE_PATH];
    double values[PE_METRICS], totals[PE_METRICS] = {0};
    unsigned events = 0, files = 0;
    int valid = 1;
    char first_names[PE_METRICS][48];
    DIR *dir = opendir(directory);
    for (struct dirent *entry = dir ? readdir(dir) : NULL; entry; entry = readdir(dir))
    {
        size_t length = strlen(entry->d_name), p = strlen(prefix), s = strlen(suffix);
        if (length > p + s && strncmp(entry->d_name, prefix, p) == 0 && entry->d_name[p] == '.' &&
                cm_equal(entry->d_name + length - s, suffix))
        {
            size_t n = 0;
            snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            char *text = cm_read(path, PE_PROFILE_BYTES, &n);
            // Model (`desc:`) lines are recorded once, from the first profile read.
            unsigned count = text ? pe_parse_profile(text, names, values, PE_METRICS, r->model_count ? NULL : r->model,
                &r->model_count) : 0;
            valid = valid && count && (!files || count == events);
            for (unsigned k = 0; valid && k < count; ++k)
            {
                valid = !files || cm_equal(names[k], first_names[k]);
                totals[k] += values[k];
                memcpy(first_names[k], names[k], sizeof(first_names[k]));
            }
            events = count;
            ++files;
            free(text);
        }
    }
    if (dir) closedir(dir);
    r->processes[arm][run] = files;
    for (unsigned k = 0; valid && files && k < events; ++k)
        pe_record(r, arm, first_names[k], "count", PE_KIND_COUNT, k == 0 || r->cache_sim || r->branch_sim, totals[k]);
    if (!valid || !files) pe_missing(r, "Ir", "count", PE_KIND_COUNT, 1, files ? "malformed or inconsistent profile output" :
        "no profile output was written");
}

BUSTER_GLOBAL_LOCAL void pe_record_rusage(PeReport *r, unsigned arm, const PeRun *run)
{
    const struct rusage *u = &run->usage;
    pe_record(r, arm, "wall_seconds", "s", PE_KIND_TIMED, 1, run->wall);
    pe_record(r, arm, "user_seconds", "s", PE_KIND_TIMED, 1, (double)u->ru_utime.tv_sec + (double)u->ru_utime.tv_usec * 1e-6);
    pe_record(r, arm, "system_seconds", "s", PE_KIND_TIMED, 1, (double)u->ru_stime.tv_sec + (double)u->ru_stime.tv_usec * 1e-6);
    pe_record(r, arm, "max_rss_kib", "KiB", PE_KIND_TIMED, 1, (double)u->ru_maxrss);
    pe_record(r, arm, "minor_faults", "count", PE_KIND_TIMED, 1, (double)u->ru_minflt);
    pe_record(r, arm, "major_faults", "count", PE_KIND_TIMED, 1, (double)u->ru_majflt);
    pe_record(r, arm, "voluntary_switches", "count", PE_KIND_TIMED, 1, (double)u->ru_nvcsw);
    pe_record(r, arm, "involuntary_switches", "count", PE_KIND_TIMED, 1, (double)u->ru_nivcsw);
}

// Version string of the measuring tool, or empty when it is absent.
BUSTER_GLOBAL_LOCAL int pe_tool_version(PeTool tool, const char *directory, char *out, size_t capacity)
{
    char path[PE_PATH];
    int result = tool == PE_TOOL_TIME;
    if (result) cm_copy(out, capacity, "wait4 rusage + CLOCK_MONOTONIC");
    else if (pe_which(tool == PE_TOOL_PERF_STAT ? "perf" : "valgrind", path, sizeof(path)))
    {
        char *argv[] = {path, "--version", NULL};
        PeRun run;
        char *text = pe_capture(argv, directory, "tool-version", &run);
        result = text && run.exited && run.status == 0;
        if (result) { cm_normalize(out, capacity, text); }
        free(text);
    }
    if (!result) cm_copy(out, capacity, "");
    return result;
}

// Join only complete paths; a refusal clears the output and cannot name another file.
BUSTER_GLOBAL_LOCAL int pe_path_join(char *out, size_t capacity, const char *parent, const char *name)
{
    size_t p = strlen(parent), n = strlen(name);
    int result = p < capacity && capacity - p > 1 && n < capacity - p - 1;
    if (result)
    {
        memcpy(out, parent, p);
        out[p] = '/';
        memcpy(out + p + 1, name, n + 1);
    }
    else if (capacity) out[0] = 0;
    return result;
}

// Remove a scratch tree at most two levels deep (preflight probes, self-test cases).
BUSTER_GLOBAL_LOCAL int pe_remove_tree(const char *root)
{
    char path[PE_PATH], file[PE_PATH];
    DIR *top = opendir(root);
    int result = top != NULL;
    for (struct dirent *entry = top ? readdir(top) : NULL; entry; entry = readdir(top))
    {
        if (entry->d_name[0] != '.')
        {
            int joined = pe_path_join(path, sizeof(path), root, entry->d_name);
            result = joined && result;
            DIR *inner = joined ? opendir(path) : NULL;
            for (struct dirent *item = inner ? readdir(inner) : NULL; item; item = readdir(inner))
            {
                if (item->d_name[0] != '.')
                {
                    int file_joined = pe_path_join(file, sizeof(file), path, item->d_name);
                    if (!file_joined || unlink(file) != 0) result = 0;
                }
            }
            if (inner) { closedir(inner); if (rmdir(path) != 0) result = 0; }
            else if (joined && unlink(path) != 0) result = 0;
        }
    }
    if (top) closedir(top);
    if (rmdir(root) != 0) result = 0;
    return result;
}

BUSTER_GLOBAL_LOCAL int pe_preflight(PeTool tool, char *const *command, unsigned command_count)
{
    char directory[] = "/tmp/performance-evidence-preflight-XXXXXX", version[128] = "", reason[512] = "";
    int ok = tool != PE_TOOL_NONE && mkdtemp(directory) != NULL;
    if (ok) ok = pe_tool_version(tool, directory, version, sizeof(version));
    if (tool != PE_TOOL_NONE && !ok) snprintf(reason, sizeof(reason), "%s is not installed or not executable",
        tool == PE_TOOL_PERF_STAT ? "perf" : "valgrind");
    if (ok && tool == PE_TOOL_PERF_STAT)
    {
        size_t n = 0;
        char *paranoid = pe_read_stream("/proc/sys/kernel/perf_event_paranoid", 64);
        if (paranoid) { cm_normalize(paranoid, 65, paranoid); printf("PREFLIGHT perf_event_paranoid=%s\n", paranoid); }
        free(paranoid);
        char events[] = PE_DEFAULT_EVENTS, output[PE_PATH], path[PE_PATH];
        snprintf(output, sizeof(output), "%s/probe.perf", directory);
        pe_which("perf", path, sizeof(path));
        char *argv[] = {path, "stat", "-x", ",", "-o", output, "-e", events, "--", "true", NULL};
        PeRun run;
        char *log = pe_capture(argv, directory, "probe", &run), *text = cm_read(output, PE_LOG_BYTES, &n);
        PeReport probe = {0};
        probe.runs = 1;
        probe.complete = 1;
        const char *problem = pe_perf_problem(log);
        if (text) pe_parse_perf(&probe, 0, text, events);
        for (unsigned i = 0; i < probe.metric_count; ++i)
            printf("PREFLIGHT event=%s status=%s%s%s\n", probe.metrics[i].name, probe.metrics[i].missing[0] ? "unavailable" : "available",
                probe.metrics[i].missing[0] ? " reason=" : "", probe.metrics[i].missing);
        ok = run.exited && run.status == 0 && text != NULL && !problem;
        if (!ok) snprintf(reason, sizeof(reason), "%s", problem ? problem : "perf stat could not count a trivial process");
        free(log);
        free(text);
    }
    if (ok && command_count && (tool == PE_TOOL_CALLGRIND || tool == PE_TOOL_CACHEGRIND))
    {
        char path[PE_PATH], *argv[PE_ARGS + 4];
        unsigned n = 0;
        pe_which("valgrind", path, sizeof(path));
        argv[n++] = path;
        argv[n++] = "--tool=none";
        argv[n++] = "--trace-children=yes";
        for (unsigned i = 0; i < command_count && n + 1 < BUSTER_ARRAY_LENGTH(argv); ++i) argv[n++] = command[i];
        argv[n] = NULL;
        PeRun run;
        char *log = pe_capture(argv, directory, "isa", &run);
        const char *problem = pe_valgrind_problem(log, &run);
        ok = !problem && run.exited && run.status == 0;
        if (!ok) snprintf(reason, sizeof(reason), "%s", problem ? problem : "the command failed under valgrind --tool=none");
        printf("PREFLIGHT isa=%s\n", problem ? "incompatible" : ok ? "compatible" : "unknown");
        free(log);
    }
    if (tool != PE_TOOL_NONE)
        printf("PREFLIGHT tool=%s status=%s version=\"%s\"%s%s%s\n", pe_tool_names[tool], ok ? "available" : "unavailable",
            version, reason[0] ? " reason=\"" : "", reason, reason[0] ? "\"" : "");
    else fputs("usage: performance-evidence preflight --tool callgrind|cachegrind|perf-stat|time [-- COMMAND ...]\n", stderr);
    if (!pe_remove_tree(directory) && tool != PE_TOOL_NONE)
    {
        fputs("PREFLIGHT cleanup=failed: scratch tree could not be completely removed\n", stderr);
        ok = 0;
    }
    int result = tool == PE_TOOL_NONE ? 2 : ok ? 0 : 3;
    return result;
}

BUSTER_GLOBAL_LOCAL int pe_classify(const char *name)
{
    const PeClaim *claim = pe_claim(name);
    int result = claim ? 0 : 2;
    if (claim)
        printf("CLAIM %s\nevidence_class=%s\ntools=%s\nroute=%s\nphysical_9700x=%s\nprimary=%s\nestablishes=%s\n"
            "does_not_establish=%s\n", claim->name, claim->evidence_class, claim->tools[0] ? claim->tools : "none",
            claim->route, cm_equal(claim->evidence_class, "zen5-native") ? "required" : "not required",
            claim->primary[0] ? claim->primary : "physical receipt", claim->establishes, claim->does_not_establish);
    else if (cm_equal(name, "mixed"))
        fputs("CLAIM mixed refused: split the change into separate claims (work-reduction, native-runtime, uarch, zen5) "
            "and measure each by its own route; record why any portion escalates to the physical host\n", stderr);
    else fputs("usage: performance-evidence classify work-reduction|native-runtime|uarch|zen5\n", stderr);
    return result;
}

// Build one measured invocation; `prefix` names this arm/run's raw files.
BUSTER_GLOBAL_LOCAL unsigned pe_command(const PeReport *r, char **argv, char *tool_path, char *option, char *cache, const char *events, const char *binary, char *const *args, unsigned arg_count)
{
    unsigned n = 0;
    if (r->tool == PE_TOOL_CALLGRIND || r->tool == PE_TOOL_CACHEGRIND)
    {
        argv[n++] = tool_path;
        argv[n++] = r->tool == PE_TOOL_CALLGRIND ? "--tool=callgrind" : "--tool=cachegrind";
        argv[n++] = "--trace-children=yes";
        argv[n++] = option;
        snprintf(cache, 64, "--cache-sim=%s", r->cache_sim ? "yes" : "no");
        argv[n++] = cache;
        argv[n++] = r->branch_sim ? "--branch-sim=yes" : "--branch-sim=no";
    }
    else if (r->tool == PE_TOOL_PERF_STAT)
    {
        argv[n++] = tool_path;
        argv[n++] = "stat";
        argv[n++] = "-x";
        argv[n++] = ",";
        argv[n++] = "-o";
        argv[n++] = option;
        argv[n++] = "-e";
        argv[n++] = (char *)events;
        argv[n++] = "--";
    }
    argv[n++] = (char *)binary;
    for (unsigned i = 0; i < arg_count && n + 1 < PE_ARGS; ++i) argv[n++] = args[i];
    argv[n] = NULL;
    return n;
}

BUSTER_GLOBAL_LOCAL int pe_compare(int argc, char **argv)
{
    PeReport *r = calloc(1, sizeof(PeReport));
    const char *claim = "", *tool = "", *paths[2] = {"", ""}, *revisions[2] = {"", ""}, *out = "", *events = PE_DEFAULT_EVENTS;
    uint64_t runs = 0, warmup = UINT64_MAX, expect = 0;
    double timeout = PE_DEFAULT_TIMEOUT, effect = PE_DEFAULT_EFFECT;
    int valid = r != NULL, separator = argc;
    for (int i = 2; valid && i < argc && separator == argc; ++i)
    {
        const char *key = argv[i], *value = i + 1 < argc ? argv[i + 1] : NULL;
        int flag = cm_equal(key, "--cache-sim") || cm_equal(key, "--branch-sim") || cm_equal(key, "--");
        if (cm_equal(key, "--")) separator = i;
        if (cm_equal(key, "--cache-sim")) r->cache_sim = 1;
        if (cm_equal(key, "--branch-sim")) r->branch_sim = 1;
        valid = flag || value;
        if (valid && !flag)
        {
            ++i;
            if (cm_equal(key, "--claim")) claim = value;
            else if (cm_equal(key, "--tool")) tool = value;
            else if (cm_equal(key, "--baseline")) paths[0] = value;
            else if (cm_equal(key, "--candidate")) paths[1] = value;
            else if (cm_equal(key, "--baseline-revision")) revisions[0] = value;
            else if (cm_equal(key, "--candidate-revision")) revisions[1] = value;
            else if (cm_equal(key, "--out")) out = value;
            else if (cm_equal(key, "--events")) events = value;
            else if (cm_equal(key, "--runs")) valid = cm_unsigned(value, &runs) && runs >= 1 && runs <= PE_RUNS_MAX;
            else if (cm_equal(key, "--warmup")) valid = cm_unsigned(value, &warmup) && warmup <= 16;
            else if (cm_equal(key, "--expect-exit")) valid = cm_unsigned(value, &expect) && expect <= 255;
            else if (cm_equal(key, "--timeout")) valid = (timeout = strtod(value, NULL)) > 0;
            else if (cm_equal(key, "--min-effect-percent")) valid = (effect = strtod(value, NULL) / 100.0) > 0 && effect < 1;
            else valid = 0;
        }
    }
    int result = 2;
    if (valid)
    {
        r->claim = pe_claim(claim);
        r->tool = pe_tool(tool);
        r->complete = 1;
        r->effect = effect;
        int instrumented = r->tool == PE_TOOL_CALLGRIND || r->tool == PE_TOOL_CACHEGRIND;
        // Instrumented counts need no cache warmup; native observations discard one pair by default.
        r->warmup = warmup != UINT64_MAX ? (unsigned)warmup : instrumented ? 0u : 1u;
        r->runs = runs ? (unsigned)runs : instrumented ? 2u : 10u;
    }
    for (unsigned arm = 0; valid && arm < 2; ++arm)
        valid = revisions[arm][0] == 0 || cm_sha(revisions[arm]);
    if (!valid || !r->claim || r->tool == PE_TOOL_NONE || !paths[0][0] || !paths[1][0] || !out[0] || separator == argc)
    {
        if (valid && !r->claim && cm_equal(claim, "mixed")) pe_classify("mixed");
        else fputs("usage: performance-evidence compare --claim work-reduction|native-runtime --tool callgrind|cachegrind|perf-stat|time "
            "--baseline BIN --candidate BIN --out NEW_DIR [--runs N] [--warmup N] [--min-effect-percent P] [--events LIST] "
            "[--cache-sim] [--branch-sim] [--expect-exit N] [--timeout S] [--baseline-revision SHA] [--candidate-revision SHA] "
            "-- ARGS ...\n", stderr);
    }
    else if (!pe_claim_allows(r->claim, r->tool))
    {
        fprintf(stderr, "PERFORMANCE_EVIDENCE_REFUSED claim=%s tool=%s: this claim needs %s via %s\n", r->claim->name,
            pe_tool_names[r->tool], r->claim->evidence_class, r->claim->tools[0] ? r->claim->tools : r->claim->route);
    }
    else if ((r->tool == PE_TOOL_TIME || r->tool == PE_TOOL_PERF_STAT) && r->runs < 6)
        fputs("PERFORMANCE_EVIDENCE_REFUSED paired observations need at least 6 pairs for the sign test to reach p <= 0.05\n", stderr);
    else if ((r->tool == PE_TOOL_CALLGRIND || r->tool == PE_TOOL_CACHEGRIND) && r->runs < 2)
        fputs("PERFORMANCE_EVIDENCE_REFUSED instrumented counts need at least 2 runs per arm to demonstrate repeatability\n", stderr);
    else if (r->tool == PE_TOOL_PERF_STAT && !pe_perf_events_valid(events))
        fputs("PERFORMANCE_EVIDENCE_REFUSED unsupported --events syntax: use event names with optional :u or :k scopes\n", stderr);
    else if (mkdir(out, 0755) != 0)
        fprintf(stderr, "PERFORMANCE_EVIDENCE_REFUSED --out %s must be a new directory so no stale output is mixed in\n", out);
    else
    {
        char sha[2][65], tool_path[PE_PATH] = "", summary[PE_PATH];
        char *args[PE_ARGS], *command[PE_ARGS];
        unsigned arg_count = 0;
        for (int i = separator + 1; i < argc && arg_count < PE_ARGS - 16; ++i) args[arg_count++] = argv[i];
        for (unsigned arm = 0; arm < 2; ++arm)
            if (!pe_sha256_file(paths[arm], sha[arm])) pe_problem(r, "unreadable binary: ", paths[arm]);
        r->a_a = r->complete && cm_equal(sha[0], sha[1]);
        if (!pe_tool_version(r->tool, out, r->tool_version, sizeof(r->tool_version)))
            pe_problem(r, "measuring tool absent: ", r->tool == PE_TOOL_PERF_STAT ? "perf" : "valgrind");
        if (r->tool != PE_TOOL_TIME) pe_which(r->tool == PE_TOOL_PERF_STAT ? "perf" : "valgrind", tool_path, sizeof(tool_path));
        unsigned total = r->warmup + r->runs;
        for (unsigned pair = 0; r->complete && pair < total; ++pair)
        {
            int measured = pair >= r->warmup;
            unsigned index = measured ? pair - r->warmup : pair;
            for (unsigned slot = 0; r->complete && slot < 2; ++slot)
            {
                unsigned arm = (pair % 2 == 0) ? slot : 1 - slot;
                char prefix[64], option[PE_PATH], cache[64], stdout_path[PE_PATH], stderr_path[PE_PATH];
                snprintf(prefix, sizeof(prefix), "%s-%c-%02u", measured ? "run" : "warmup", arm ? 'B' : 'A', index);
                const char *suffix = r->tool == PE_TOOL_CALLGRIND ? "callgrind" : "cachegrind";
                if (r->tool == PE_TOOL_PERF_STAT) snprintf(option, sizeof(option), "%s/%s.perf", out, prefix);
                else snprintf(option, sizeof(option), "--%s-out-file=%s/%s.%%p.%s", suffix, out, prefix, suffix);
                snprintf(stdout_path, sizeof(stdout_path), "%s/%s.stdout", out, prefix);
                snprintf(stderr_path, sizeof(stderr_path), "%s/%s.stderr", out, prefix);
                pe_command(r, command, tool_path, option, cache, events, paths[arm], args, arg_count);
                PeRun run = pe_run(command, stdout_path, stderr_path, timeout);
                size_t n = 0;
                char *log = cm_read(stderr_path, PE_LOG_BYTES, &n);
                const char *problem = r->tool == PE_TOOL_PERF_STAT ? pe_perf_problem(log) :
                    r->tool == PE_TOOL_TIME ? NULL : pe_valgrind_problem(log, &run);
                if (problem && (r->tool != PE_TOOL_PERF_STAT || !strstr(problem, "not supported"))) pe_problem(r, problem, NULL);
                else if (!run.started || run.timed_out || !run.exited || run.status != (int)expect)
                {
                    char text[160];
                    snprintf(text, sizeof(text), "%s exited %s %d (expected exit %u)", prefix, run.timed_out ? "by timeout" :
                        run.exited ? "with status" : "by signal", run.exited ? run.status : run.signal, (unsigned)expect);
                    pe_problem(r, text, NULL);
                }
                else if (measured && r->tool == PE_TOOL_TIME) { pe_record_rusage(r, arm, &run); r->processes[arm][index] = 1; }
                else if (measured && r->tool == PE_TOOL_PERF_STAT)
                {
                    char *text = cm_read(option, PE_LOG_BYTES, &n);
                    if (text) pe_parse_perf(r, arm, text, events);
                    else pe_problem(r, "perf wrote no output: ", option);
                    r->processes[arm][index] = 1;
                    free(text);
                }
                else if (measured) pe_collect_profiles(r, arm, index, out, prefix, suffix);
                free(log);
            }
        }
        const char *identity[6] = {paths[0], sha[0], revisions[0], paths[1], sha[1], revisions[1]};
        snprintf(summary, sizeof(summary), "%s/summary.json", out);
        FILE *f = fopen(summary, "wb");
        if (!f) pe_problem(r, "cannot write ", summary);
        result = pe_finish(r, f, identity, args, arg_count);
        if (f) fclose(f);
    }
    free(r);
    return result;
}

#include "performance_evidence_test.h"

int main(int argc, char **argv)
{
    int result = 2;
    if (argc == 2 && cm_equal(argv[1], "--self-test")) result = pe_self_test();
    else if (argc == 3 && cm_equal(argv[1], "classify")) result = pe_classify(argv[2]);
    else if (argc >= 2 && cm_equal(argv[1], "preflight"))
    {
        PeTool tool = argc >= 4 && cm_equal(argv[2], "--tool") ? pe_tool(argv[3]) : PE_TOOL_NONE;
        int command = argc >= 5 && cm_equal(argv[4], "--") ? 5 : argc;
        result = (argc == 4 || command == 5) ? pe_preflight(tool, argv + command, (unsigned)(argc - command)) : pe_preflight(PE_TOOL_NONE, NULL, 0);
    }
    else if (argc >= 2 && cm_equal(argv[1], "compare")) result = pe_compare(argc, argv);
    else fputs("usage: performance-evidence --self-test | classify CLAIM | preflight --tool T [-- COMMAND ...] | compare ...\n", stderr);
    return result;
}
