#define _POSIX_C_SOURCE 200809L
// This qualification is limited to the local-repository Git fixture in the
// merge-gate suite; Git strips GIT_CONFIG_COUNT across local transport.
#include "ci_metrics_json.h"
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define HK_SESSION_SLOTS 8192u
#define HK_CHILD_SLOTS 32768u
#define HK_DEADLINE_MS 120000u
#define HK_TRACE_LIMIT CM_BYTES

typedef struct HkSession HkSession;
struct HkSession
{
    const char *sid;
    unsigned version, start, exit, atexit;
};

typedef struct HkChild HkChild;
struct HkChild
{
    const char *id;
    unsigned session, started, exited;
};

typedef struct HkReport HkReport;
struct HkReport
{
    unsigned sessions, events, child_starts, child_exits;
    unsigned automatic, automatic_children, detached;
};

typedef struct HkRun HkRun;
struct HkRun
{
    int code, timed_out, group_gone;
};

enum { HK_ACTIVE, HK_GUARDED };

BUSTER_GLOBAL_LOCAL const char *hk_guard_keys[] = {
    "maintenance.auto", "maintenance.autoDetach",
    "maintenance.loose-objects.auto", "maintenance.loose-objects.enabled",
    "gc.auto", "gc.autoDetach", "receive.autogc",
    "maintenance.auto", "gc.auto", "receive.autogc"
};

BUSTER_GLOBAL_LOCAL const char *hk_guard_values[] = {
    "true", "false", "1", "true", "1", "false", "true",
    "false", "0", "false"
};

BUSTER_GLOBAL_LOCAL uint64_t hk_now_ms(int *valid)
{
    struct timespec now;
    uint64_t result = 0;
    *valid = clock_gettime(CLOCK_MONOTONIC, &now) == 0;
    if (*valid) result = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    return result;
}

BUSTER_GLOBAL_LOCAL void hk_pause(void)
{
    struct timespec delay = {0, 5000000};
    nanosleep(&delay, NULL);
}

BUSTER_GLOBAL_LOCAL int hk_set_environment(int mode, const char *trace,
    const char *temporary, const char *home)
{
    unsigned count = mode == HK_ACTIVE ? 7u : 10u;
    int valid = setenv("GIT_CONFIG_COUNT", mode == HK_ACTIVE ? "7" : "10", 1) == 0;
    valid = valid && setenv("GIT_CONFIG_NOSYSTEM", "1", 1) == 0;
    valid = valid && setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1) == 0;
    valid = valid && setenv("GIT_CONFIG_PARAMETERS", "", 1) == 0;
    valid = valid && setenv("GIT_TRACE2_EVENT_NESTING", "10", 1) == 0;
    valid = valid && setenv("TMPDIR", temporary, 1) == 0;
    valid = valid && setenv("HOME", home, 1) == 0;
    if (trace && trace[0]) valid = valid && setenv("GIT_TRACE2_EVENT", trace, 1) == 0;
    else valid = valid && unsetenv("GIT_TRACE2_EVENT") == 0;
    for (unsigned i = 0; valid && i < count; ++i)
    {
        char key_name[40], value_name[42];
        int key_size = snprintf(key_name, sizeof(key_name), "GIT_CONFIG_KEY_%u", i);
        int value_size = snprintf(value_name, sizeof(value_name), "GIT_CONFIG_VALUE_%u", i);
        valid = key_size > 0 && (size_t)key_size < sizeof(key_name) &&
            value_size > 0 && (size_t)value_size < sizeof(value_name);
        if (valid)
        {
            valid = setenv(key_name, hk_guard_keys[i], 1) == 0;
            valid = valid && setenv(value_name, hk_guard_values[i], 1) == 0;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_group_gone(pid_t group)
{
    int gone = 0;
    if (kill(-group, 0) < 0 && errno == ESRCH) gone = 1;
    return gone;
}

BUSTER_GLOBAL_LOCAL int hk_wait_group(pid_t group, int terminate)
{
    int gone = 0;
    for (unsigned i = 0; !gone && i < 200; ++i)
    {
        gone = hk_group_gone(group);
        if (!gone) hk_pause();
    }
    if (!gone && terminate)
    {
        kill(-group, SIGKILL);
        for (unsigned i = 0; !gone && i < 200; ++i)
        {
            gone = hk_group_gone(group);
            if (!gone) hk_pause();
        }
    }
    return gone;
}

BUSTER_GLOBAL_LOCAL int hk_run(char *const arguments[], const char *directory,
    int mode, const char *trace, const char *temporary, const char *home,
    unsigned deadline_ms, HkRun *out)
{
    pid_t child = fork();
    int valid = child >= 0, reaped = 0, timed_out = 0, status = 0, clock_ok = 0;
    uint64_t started = 0;
    out->code = -1;
    out->timed_out = 0;
    out->group_gone = 0;
    if (child == 0)
    {
        if (setpgid(0, 0) != 0 || chdir(directory) != 0 ||
            !hk_set_environment(mode, trace, temporary, home)) _exit(125);
        execvp(arguments[0], arguments);
        _exit(127);
    }
    if (child > 0)
    {
        setpgid(child, child);
        started = hk_now_ms(&clock_ok);
        valid = clock_ok;
        while (valid && !reaped && !timed_out)
        {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) reaped = 1;
            else if (waited < 0 && errno != EINTR) valid = 0;
            else
            {
                uint64_t now = hk_now_ms(&clock_ok);
                valid = clock_ok;
                if (valid && now - started >= deadline_ms) timed_out = 1;
                else if (valid) hk_pause();
            }
        }
        if (timed_out || !valid) kill(-child, SIGKILL);
        if (!reaped && child > 0)
        {
            for (unsigned i = 0; !reaped && i < 200; ++i)
            {
                pid_t waited = waitpid(child, &status, WNOHANG);
                if (waited == child) reaped = 1;
                else if (waited < 0 && errno != EINTR) valid = 0;
                if (!reaped) hk_pause();
            }
            if (!reaped) valid = 0;
        }
        if (reaped && WIFEXITED(status)) out->code = WEXITSTATUS(status);
        else if (!(reaped && timed_out && WIFSIGNALED(status))) valid = 0;
        out->group_gone = hk_wait_group(child, 1);
        if (!out->group_gone) valid = 0;
        out->timed_out = timed_out;
        if (timed_out) out->code = -1;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_auto_argv(const CmJson *json, unsigned node,
    HkReport *report)
{
    int valid = node && node <= json->count && json->tokens[node].kind == 'a';
    unsigned words = 0;
    int automatic = 0, detached = 0;
    const char *before = "", *previous = "";
    if (valid)
    {
        for (unsigned item = json->tokens[node].child; item; item = json->tokens[item].next)
        {
            valid = valid && json->tokens[item].kind == 's' &&
                json->tokens[item].text != NULL;
            if (valid)
            {
                const char *word = json->tokens[item].text;
                valid = words != 0 || word[0] != 0;
                if (valid)
                {
                    if ((cm_equal(before, "maintenance") && cm_equal(previous, "run") &&
                            cm_equal(word, "--auto")) ||
                        (cm_equal(previous, "gc") && cm_equal(word, "--auto"))) automatic = 1;
                    if (cm_equal(word, "--detach")) detached = 1;
                    before = previous;
                    previous = word;
                    ++words;
                }
            }
        }
        valid = valid && words != 0;
    }
    if (valid)
    {
        report->automatic += automatic != 0;
        report->detached += detached != 0;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_session_slot(HkSession *sessions, const char *sid,
    int create, unsigned *used)
{
    int result = -1;
    unsigned start = (unsigned)(cm_hash(sid) & (HK_SESSION_SLOTS - 1));
    for (unsigned i = 0; i < HK_SESSION_SLOTS && result < 0; ++i)
    {
        unsigned slot = (start + i) & (HK_SESSION_SLOTS - 1);
        if (!sessions[slot].sid)
        {
            if (create)
            {
                sessions[slot].sid = sid;
                ++*used;
                result = (int)slot;
            }
            else i = HK_SESSION_SLOTS;
        }
        else if (cm_equal(sessions[slot].sid, sid)) result = (int)slot;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int hk_child_slot(HkChild *children, unsigned session,
    const char *id, int create)
{
    uint64_t hash = cm_hash(id) ^ ((uint64_t)session * UINT64_C(11400714819323198485));
    unsigned start = (unsigned)(hash & (HK_CHILD_SLOTS - 1));
    int result = -1;
    for (unsigned i = 0; i < HK_CHILD_SLOTS && result < 0; ++i)
    {
        unsigned slot = (start + i) & (HK_CHILD_SLOTS - 1);
        if (!children[slot].id)
        {
            if (create)
            {
                children[slot].id = id;
                children[slot].session = session;
                result = (int)slot;
            }
            else i = HK_CHILD_SLOTS;
        }
        else if (children[slot].session == session && cm_equal(children[slot].id, id))
            result = (int)slot;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int hk_parse_trace_data(const char *bytes, size_t length,
    HkReport *out)
{
    char *array = NULL;
    HkSession *sessions = NULL;
    HkChild *children = NULL;
    CmJson json = {0};
    HkReport report = {0};
    size_t used = 0, line_start = 0;
    unsigned session_count = 0;
    int valid = bytes && length > 0 && length <= HK_TRACE_LIMIT &&
        bytes[length - 1] == '\n';
    if (valid)
    {
        array = malloc(HK_TRACE_LIMIT + 1u);
        sessions = calloc(HK_SESSION_SLOTS, sizeof(*sessions));
        children = calloc(HK_CHILD_SLOTS, sizeof(*children));
        valid = array && sessions && children;
    }
    if (valid)
    {
        array[used++] = '[';
        for (size_t i = 0; valid && i < length; ++i)
        {
            if (bytes[i] == '\0' || bytes[i] == '\r') valid = 0;
            else if (bytes[i] == '\n')
            {
                valid = i > line_start && used + 2 < HK_TRACE_LIMIT;
                if (valid)
                {
                    if (i + 1 < length) array[used++] = ',';
                    line_start = i + 1;
                }
            }
            else
            {
                valid = used + 2 < HK_TRACE_LIMIT;
                if (valid) array[used++] = bytes[i];
            }
        }
        valid = valid && line_start == length;
        if (valid) array[used++] = ']';
    }
    if (valid)
    {
        json = cm_json_parse(array, used);
        valid = json.valid && json.tokens[1].kind == 'a';
    }
    if (valid)
    {
        for (unsigned event = json.tokens[1].child; valid && event;
             event = json.tokens[event].next)
        {
            unsigned event_field = cm_member(&json, event, "event");
            unsigned sid_field = cm_member(&json, event, "sid");
            const char *name = cm_get(&json, event, "event");
            const char *sid = cm_get(&json, event, "sid");
            int slot = -1;
            valid = json.tokens[event].kind == 'o' && event_field && sid_field &&
                json.tokens[event_field].kind == 's' && json.tokens[sid_field].kind == 's' &&
                sid[0] != 0;
            if (valid)
            {
                slot = hk_session_slot(sessions, sid, 1, &session_count);
                valid = slot >= 0;
            }
            if (valid)
            {
                HkSession *session = &sessions[(unsigned)slot];
                valid = session->atexit == 0;
                if (valid)
                {
                    ++report.events;
                    if (cm_equal(name, "version"))
                    {
                        unsigned exe = cm_member(&json, event, "exe");
                        valid = session->version == 0 && session->start == 0 &&
                            exe && json.tokens[exe].kind == 's' && cm_get(&json, event, "exe")[0] != 0;
                        if (valid) ++session->version;
                    }
                    else if (cm_equal(name, "start"))
                    {
                        unsigned argv = cm_member(&json, event, "argv");
                        valid = session->version == 1 && session->start == 0 &&
                            hk_auto_argv(&json, argv, &report);
                        if (valid) ++session->start;
                    }
                    else if (cm_equal(name, "exit"))
                    {
                        valid = session->start == 1 && session->exit == 0;
                        if (valid) ++session->exit;
                    }
                    else if (cm_equal(name, "atexit"))
                    {
                        valid = session->exit == 1 && session->atexit == 0;
                        if (valid) ++session->atexit;
                    }
                    else if (cm_equal(name, "child_start"))
                    {
                        unsigned field = cm_member(&json, event, "child_id");
                        uint64_t child_number = 0;
                        unsigned auto_before = report.automatic;
                        valid = session->start == 1 && session->exit == 0 && field && json.tokens[field].kind == 'v' &&
                            cm_unsigned(cm_value(&json, field), &child_number) &&
                            hk_auto_argv(&json, cm_member(&json, event, "argv"), &report);
                        if (valid && report.automatic > auto_before) ++report.automatic_children;
                        if (valid)
                        {
                            unsigned id_field = cm_member(&json, event, "child_id");
                            const char *id = cm_value(&json, id_field);
                            int child_slot = hk_child_slot(children, (unsigned)slot, id, 1);
                            valid = child_slot >= 0 && !children[(unsigned)child_slot].started;
                            if (valid)
                            {
                                children[(unsigned)child_slot].started = 1;
                                ++report.child_starts;
                            }
                        }
                    }
                    else if (cm_equal(name, "child_exit"))
                    {
                        unsigned field = cm_member(&json, event, "child_id");
                        uint64_t child_number = 0;
                        valid = session->start == 1 && session->exit == 0 && field && json.tokens[field].kind == 'v' &&
                            cm_unsigned(cm_value(&json, field), &child_number);
                        if (valid)
                        {
                            int child_slot = hk_child_slot(children, (unsigned)slot,
                                cm_value(&json, field), 0);
                            valid = child_slot >= 0 && children[(unsigned)child_slot].started &&
                                !children[(unsigned)child_slot].exited;
                            if (valid)
                            {
                                children[(unsigned)child_slot].exited = 1;
                                ++report.child_exits;
                            }
                        }
                    }
                }
            }
        }
    }
    if (valid)
    {
        report.sessions = session_count;
        valid = session_count > 0;
        for (unsigned i = 0; valid && i < HK_SESSION_SLOTS; ++i)
        {
            if (sessions[i].sid)
                valid = sessions[i].version == 1 && sessions[i].start == 1 &&
                    sessions[i].exit == 1 && sessions[i].atexit == 1;
        }
        for (unsigned i = 0; valid && i < HK_CHILD_SLOTS; ++i)
        {
            if (children[i].id) valid = children[i].started == 1 && children[i].exited == 1;
        }
        valid = valid && report.child_starts == report.child_exits;
    }
    if (out) *out = report;
    cm_json_free(&json);
    free(children);
    free(sessions);
    free(array);
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_parse_trace_file(const char *path, HkReport *report)
{
    FILE *file = fopen(path, "rb");
    char *bytes = malloc(HK_TRACE_LIMIT + 1u);
    size_t length = 0;
    int valid = file && bytes;
    if (valid)
    {
        length = fread(bytes, 1, HK_TRACE_LIMIT, file);
        valid = !ferror(file) && feof(file) && length > 0;
    }
    if (file && fclose(file) != 0) valid = 0;
    if (valid) valid = hk_parse_trace_data(bytes, length, report);
    free(bytes);
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_trace_parser_controls(void)
{
    const char *complete =
        "{\"event\":\"version\",\"sid\":\"s1\",\"exe\":\"git\"}\n"
        "{\"event\":\"start\",\"sid\":\"s1\",\"argv\":[\"git\",\"version\"]}\n"
        "{\"event\":\"exit\",\"sid\":\"s1\",\"code\":0}\n"
        "{\"event\":\"atexit\",\"sid\":\"s1\",\"code\":0}\n";
    const char *missing_end =
        "{\"event\":\"version\",\"sid\":\"s2\",\"exe\":\"git\"}\n"
        "{\"event\":\"start\",\"sid\":\"s2\",\"argv\":[\"git\",\"version\"]}\n"
        "{\"event\":\"exit\",\"sid\":\"s2\",\"code\":0}\n";
    const char *incomplete_child =
        "{\"event\":\"version\",\"sid\":\"s3\",\"exe\":\"git\"}\n"
        "{\"event\":\"start\",\"sid\":\"s3\",\"argv\":[\"git\",\"commit\"]}\n"
        "{\"event\":\"child_start\",\"sid\":\"s3\",\"child_id\":1,\"argv\":[\"git\",\"maintenance\",\"run\",\"--auto\"]}\n"
        "{\"event\":\"exit\",\"sid\":\"s3\",\"code\":0}\n"
        "{\"event\":\"atexit\",\"sid\":\"s3\",\"code\":0}\n";
    const char *wrong_argv_type =
        "{\"event\":\"version\",\"sid\":\"s4\",\"exe\":\"git\"}\n"
        "{\"event\":\"start\",\"sid\":\"s4\",\"argv\":[\"git\",7,\"version\"]}\n"
        "{\"event\":\"exit\",\"sid\":\"s4\",\"code\":0}\n"
        "{\"event\":\"atexit\",\"sid\":\"s4\",\"code\":0}\n";
    const char *empty_argv =
        "{\"event\":\"version\",\"sid\":\"s5\",\"exe\":\"git\"}\n"
        "{\"event\":\"start\",\"sid\":\"s5\",\"argv\":[]}\n"
        "{\"event\":\"exit\",\"sid\":\"s5\",\"code\":0}\n"
        "{\"event\":\"atexit\",\"sid\":\"s5\",\"code\":0}\n";
    const char *empty_argv0 =
        "{\"event\":\"version\",\"sid\":\"s6\",\"exe\":\"git\"}\n"
        "{\"event\":\"start\",\"sid\":\"s6\",\"argv\":[\"\",\"version\"]}\n"
        "{\"event\":\"exit\",\"sid\":\"s6\",\"code\":0}\n"
        "{\"event\":\"atexit\",\"sid\":\"s6\",\"code\":0}\n";
    const char *malformed = "{\"event\":\n";
    HkReport report = {0};
    int valid = hk_parse_trace_data(complete, strlen(complete), &report) &&
        report.sessions == 1 && report.events == 4;
    valid = valid && !hk_parse_trace_data(missing_end, strlen(missing_end), &report);
    valid = valid && !hk_parse_trace_data(incomplete_child, strlen(incomplete_child), &report);
    valid = valid && !hk_parse_trace_data(wrong_argv_type, strlen(wrong_argv_type), &report);
    valid = valid && !hk_parse_trace_data(empty_argv, strlen(empty_argv), &report);
    valid = valid && !hk_parse_trace_data(empty_argv0, strlen(empty_argv0), &report);
    valid = valid && !hk_parse_trace_data(complete, strlen(complete) - 1, &report);
    valid = valid && !hk_parse_trace_data(malformed, strlen(malformed), &report);
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_make_file(const char *path, unsigned value)
{
    FILE *file = fopen(path, "wb");
    int valid = file != NULL;
    if (valid)
    {
        valid = fprintf(file, "distinct fixture payload %u\n", value) > 0;
        if (fclose(file) != 0) valid = 0;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_successful_run(char *const arguments[],
    const char *directory, int mode, const char *trace, const char *temporary,
    const char *home, unsigned deadline_ms)
{
    HkRun run = {-1, 0, 0};
    int valid = hk_run(arguments, directory, mode, trace, temporary, home,
        deadline_ms, &run);
    valid = valid && !run.timed_out && run.code == 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_prepare_active_control(const char *root,
    const char *repo, const char *temporary, const char *trace)
{
    char *init[] = {"git", "init", "-q", "-b", "main", (char *)repo, NULL};
    char *name[] = {"git", "-C", (char *)repo, "config", "user.name", "Fixture", NULL};
    char *email[] = {"git", "-C", (char *)repo, "config", "user.email",
        "fixture@example.invalid", NULL};
    char *add[] = {"git", "-C", (char *)repo, "add", "--all", NULL};
    char *commit[] = {"git", "-C", (char *)repo, "commit", "-m",
        "threshold qualification", NULL};
    char path[512];
    int valid = hk_successful_run(init, root, HK_ACTIVE, trace, temporary, root, HK_DEADLINE_MS);
    valid = valid && hk_successful_run(name, root, HK_ACTIVE, trace, temporary, root, HK_DEADLINE_MS);
    valid = valid && hk_successful_run(email, root, HK_ACTIVE, trace, temporary, root, HK_DEADLINE_MS);
    for (unsigned i = 0; valid && i < 40; ++i)
    {
        int size = snprintf(path, sizeof(path), "%s/payload-%02u.bin", repo, i);
        valid = size > 0 && (size_t)size < sizeof(path) && hk_make_file(path, i);
    }
    valid = valid && hk_successful_run(add, root, HK_ACTIVE, trace, temporary, root, HK_DEADLINE_MS);
    valid = valid && hk_successful_run(commit, root, HK_ACTIVE, trace, temporary, root, HK_DEADLINE_MS);
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_directory_empty(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry = NULL;
    int valid = directory != NULL;
    errno = 0;
    while (valid && (entry = readdir(directory)) != NULL)
        valid = cm_equal(entry->d_name, ".") || cm_equal(entry->d_name, "..");
    if (errno != 0) valid = 0;
    if (directory && closedir(directory) != 0) valid = 0;
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_safe_root(const char *root)
{
    static const char prefix[] = "/tmp/buster-native-retirement-fixture.";
    struct stat info;
    int valid = root && root[0] == '/' && strncmp(root, prefix, sizeof(prefix) - 1) == 0 &&
        strlen(root) < 256 && lstat(root, &info) == 0 && S_ISDIR(info.st_mode) &&
        !S_ISLNK(info.st_mode);
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_cleanup_root(const char *root, const char *temporary)
{
    char *remove[] = {"rm", "-rf", "--", (char *)root, NULL};
    HkRun run = {-1, 0, 0};
    struct stat info;
    int valid = hk_safe_root(root);
    if (valid)
    {
        valid = hk_run(remove, "/tmp", HK_GUARDED, NULL, temporary, root,
            HK_DEADLINE_MS, &run);
        valid = valid && !run.timed_out && run.code == 0 &&
            lstat(root, &info) != 0 && errno == ENOENT;
    }
    return valid;
}

int main(void)
{
    char root[] = "/tmp/buster-native-retirement-fixture.XXXXXX";
    char repo[256], temporary[256], control_trace[256], protected_trace[256];
    char *version[] = {"git", "--version", NULL};
    char *exec_path[] = {"git", "--exec-path", NULL};
    char *python[] = {"python3", "-B", "tools/native_retirement_merge_gate_test.py",
        "-v", NULL};
    HkReport active = {0}, guarded = {0};
    HkRun timeout = {-1, 0, 0};
    char *sleeping[] = {"sleep", "2", NULL};
    int valid = hk_trace_parser_controls();
    int have_root = 0;
    if (!valid) fprintf(stderr, "Trace2 parser controls failed closed incorrectly.\n");
    if (valid)
    {
        char *made = mkdtemp(root);
        have_root = made != NULL;
        valid = have_root;
    }
    if (valid)
    {
        int repo_size = snprintf(repo, sizeof(repo), "%s/control", root);
        int temp_size = snprintf(temporary, sizeof(temporary), "%s/python-tmp", root);
        int control_size = snprintf(control_trace, sizeof(control_trace), "%s/control.jsonl", root);
        int protected_size = snprintf(protected_trace, sizeof(protected_trace), "%s/protected.jsonl", root);
        valid = repo_size > 0 && (size_t)repo_size < sizeof(repo) &&
            temp_size > 0 && (size_t)temp_size < sizeof(temporary) &&
            control_size > 0 && (size_t)control_size < sizeof(control_trace) &&
            protected_size > 0 && (size_t)protected_size < sizeof(protected_trace) &&
            mkdir(temporary, 0700) == 0;
    }
    if (valid)
    {
        FILE *trace = fopen(control_trace, "wb");
        valid = trace && fclose(trace) == 0;
    }
    if (valid)
    {
        printf("Housekeeping control Git version:\n");
        valid = hk_successful_run(version, "/tmp", HK_ACTIVE, control_trace,
            temporary, root, HK_DEADLINE_MS);
        valid = valid && hk_successful_run(exec_path, "/tmp", HK_ACTIVE, control_trace,
            temporary, root, HK_DEADLINE_MS);
    }
    if (valid)
    {
        valid = hk_prepare_active_control(root, repo, temporary, control_trace);
        valid = valid && hk_parse_trace_file(control_trace, &active);
        valid = valid && active.sessions > 0 && active.child_starts > 0 &&
            active.automatic_children > 0 && active.detached == 0;
        if (!valid) fprintf(stderr, "Forced-threshold control did not observe foreground Git auto-housekeeping.\n");
    }
    if (valid)
    {
        FILE *trace = fopen(protected_trace, "wb");
        valid = trace && fclose(trace) == 0;
    }
    if (valid)
    {
        printf("Running protected merge-gate Git fixture suite:\n");
        valid = hk_successful_run(python, ".", HK_GUARDED, protected_trace,
            temporary, root, 300000u);
        valid = valid && hk_directory_empty(temporary);
        valid = valid && hk_parse_trace_file(protected_trace, &guarded);
        valid = valid && guarded.sessions > 0 && guarded.automatic == 0 &&
            guarded.detached == 0 && guarded.child_starts == guarded.child_exits;
        if (!valid) fprintf(stderr, "Protected suite did not produce a complete, housekeeping-free Trace2 record.\n");
    }
    if (valid)
    {
        printf("Trace2 positive control: %u Git sessions, %u child starts, %u auto commands, %u detach arguments.\n",
            active.sessions, active.child_starts, active.automatic_children, active.detached);
        printf("Protected merge-gate suite: %u Git sessions, %u child starts, %u auto commands, %u detach arguments.\n",
            guarded.sessions, guarded.child_starts, guarded.automatic, guarded.detached);
        printf("Checking bounded process-group timeout:\n");
        valid = hk_run(sleeping, "/tmp", HK_GUARDED, NULL, temporary, root,
            25u, &timeout);
        valid = valid && timeout.timed_out && timeout.group_gone;
    }
    if (have_root)
    {
        if (valid)
        {
            int removed = hk_cleanup_root(root, temporary);
            valid = valid && removed;
            if (!removed) fprintf(stderr, "Strict evidence-root cleanup failed; retained path: %s\n", root);
        }
        else fprintf(stderr, "Qualification evidence retained at %s\n", root);
    }
    if (valid)
        puts("Native-retirement fixture housekeeping qualification passed.");
    else
        fputs("Native-retirement fixture housekeeping qualification failed.\n", stderr);
    return valid ? 0 : 1;
}
