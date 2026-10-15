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
#define HK_SID_PREVIEW_CAP 64u
#define HK_CHILD_PREVIEW_CAP 24u

typedef struct HkLocation HkLocation;
struct HkLocation
{
    size_t offset;
    unsigned line, event;
    int set, offset_known;
    char event_type[24], sid[HK_SID_PREVIEW_CAP], child_id[HK_CHILD_PREVIEW_CAP];
};

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
    HkLocation start_event;
};

typedef struct HkIdArena HkIdArena;
struct HkIdArena
{
    char *bytes;
    size_t used, capacity;
};

typedef struct HkReport HkReport;
struct HkReport
{
    unsigned sessions, events, child_starts, child_exits;
    unsigned automatic, automatic_children, detached;
    unsigned sessions_complete, sessions_incomplete, children_complete, children_incomplete;
    unsigned events_seen;
    unsigned json_tokens, json_arena_bytes;
    size_t trace_bytes, failure_offset;
    unsigned failure_line, failure_event;
    int failure_offset_known, parser_valid, complete, io_error_number;
    char failure_phase[24], failure_reason[40];
    char failure_event_type[24], failure_sid[HK_SID_PREVIEW_CAP], failure_child_id[HK_CHILD_PREVIEW_CAP];
    HkLocation first_automatic, first_detached;
};

typedef struct HkRun HkRun;
struct HkRun
{
    int code, timed_out, group_gone, spawned, reaped;
    int signal_number, error_number;
};

typedef struct HkDirectoryReport HkDirectoryReport;
struct HkDirectoryReport
{
    unsigned entries;
    int error_number, close_error_number;
};

enum { HK_ACTIVE, HK_GUARDED };

BUSTER_GLOBAL_LOCAL void hk_copy_safe_identifier(char *destination,
    size_t capacity, const char *source)
{
    size_t length = 0;
    int valid = source != NULL;
    while (valid && source[length] && length + 1 < capacity)
    {
        unsigned char c = (unsigned char)source[length];
        valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (valid) destination[length++] = (char)c;
    }
    if (capacity)
    {
        if (valid && source[length] == 0) destination[length] = 0;
        else cm_copy(destination, capacity, "invalid");
    }
}

BUSTER_GLOBAL_LOCAL const char *hk_event_label(const char *name)
{
    const char *result = "other";
    if (cm_equal(name, "version")) result = "version";
    else if (cm_equal(name, "start")) result = "start";
    else if (cm_equal(name, "exit")) result = "exit";
    else if (cm_equal(name, "atexit")) result = "atexit";
    else if (cm_equal(name, "child_start")) result = "child_start";
    else if (cm_equal(name, "child_exit")) result = "child_exit";
    return result;
}

BUSTER_GLOBAL_LOCAL void hk_location_set(HkLocation *location, size_t offset,
    unsigned line, unsigned event, const char *event_type, const char *sid,
    const char *child_id)
{
    if (!location->set)
    {
        location->set = 1;
        location->offset_known = 1;
        location->offset = offset;
        location->line = line;
        location->event = event;
        hk_copy_safe_identifier(location->event_type, sizeof(location->event_type),
            event_type ? event_type : "");
        hk_copy_safe_identifier(location->sid, sizeof(location->sid), sid ? sid : "");
        hk_copy_safe_identifier(location->child_id, sizeof(location->child_id),
            child_id ? child_id : "");
    }
}

BUSTER_GLOBAL_LOCAL void hk_report_failure(HkReport *report,
    const char *phase, const char *reason, const HkLocation *location)
{
    if (report && !report->failure_reason[0])
    {
        cm_copy(report->failure_phase, sizeof(report->failure_phase), phase);
        cm_copy(report->failure_reason, sizeof(report->failure_reason), reason);
        if (location && location->set)
        {
            report->failure_offset = location->offset;
            report->failure_offset_known = location->offset_known;
            report->failure_line = location->line;
            report->failure_event = location->event;
            cm_copy(report->failure_event_type, sizeof(report->failure_event_type),
                location->event_type);
            cm_copy(report->failure_sid, sizeof(report->failure_sid), location->sid);
            cm_copy(report->failure_child_id, sizeof(report->failure_child_id),
                location->child_id);
        }
    }
}

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
    out->spawned = child > 0;
    out->reaped = 0;
    out->signal_number = 0;
    out->error_number = child < 0 ? errno : 0;
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
        if (!clock_ok) out->error_number = errno;
        while (valid && !reaped && !timed_out)
        {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) reaped = 1;
            else if (waited < 0 && errno != EINTR)
            {
                valid = 0;
                out->error_number = errno;
            }
            else
            {
                uint64_t now = hk_now_ms(&clock_ok);
                valid = clock_ok;
                if (!clock_ok) out->error_number = errno;
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
                else if (waited < 0 && errno != EINTR)
                {
                    valid = 0;
                    out->error_number = errno;
                }
                if (!reaped) hk_pause();
            }
            if (!reaped) valid = 0;
        }
        out->reaped = reaped;
        if (reaped && WIFEXITED(status)) out->code = WEXITSTATUS(status);
        else if (reaped && WIFSIGNALED(status)) out->signal_number = WTERMSIG(status);
        if (!(reaped && WIFEXITED(status)) &&
            !(reaped && timed_out && WIFSIGNALED(status))) valid = 0;
        out->group_gone = hk_wait_group(child, 1);
        if (!out->group_gone) valid = 0;
        out->timed_out = timed_out;
        if (timed_out) out->code = -1;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_auto_argv(const CmJson *json, unsigned node,
    HkReport *report, const HkLocation *location)
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
        if (automatic && location)
            hk_location_set(&report->first_automatic, location->offset, location->line,
                location->event, location->event_type, location->sid, location->child_id);
        if (detached && location)
            hk_location_set(&report->first_detached, location->offset, location->line,
                location->event, location->event_type, location->sid, location->child_id);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL const char *hk_id_arena_copy(HkIdArena *arena,
    const char *value)
{
    const char *copy = NULL;
    size_t length = value ? strlen(value) : 0;
    int valid = length && arena && arena->bytes && arena->used <= arena->capacity &&
        length < arena->capacity - arena->used;
    if (valid)
    {
        char *storage = arena->bytes + arena->used;
        memcpy(storage, value, length + 1);
        arena->used += length + 1;
        copy = storage;
    }
    return copy;
}

BUSTER_GLOBAL_LOCAL int hk_session_slot(HkSession *sessions, const char *sid,
    int create, unsigned *used, HkIdArena *identifiers, int *storage_failed)
{
    int result = -1;
    unsigned start = (unsigned)(cm_hash(sid) & (HK_SESSION_SLOTS - 1));
    *storage_failed = 0;
    for (unsigned i = 0; i < HK_SESSION_SLOTS && result < 0 &&
        !*storage_failed; ++i)
    {
        unsigned slot = (start + i) & (HK_SESSION_SLOTS - 1);
        if (!sessions[slot].sid)
        {
            if (create)
            {
                sessions[slot].sid = hk_id_arena_copy(identifiers, sid);
                if (!sessions[slot].sid) *storage_failed = 1;
                else
                {
                    ++*used;
                    result = (int)slot;
                }
            }
            else i = HK_SESSION_SLOTS;
        }
        else if (cm_equal(sessions[slot].sid, sid)) result = (int)slot;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int hk_child_slot(HkChild *children, unsigned session,
    const char *id, int create, HkIdArena *identifiers, int *storage_failed)
{
    uint64_t hash = cm_hash(id) ^ ((uint64_t)session * UINT64_C(11400714819323198485));
    unsigned start = (unsigned)(hash & (HK_CHILD_SLOTS - 1));
    int result = -1;
    *storage_failed = 0;
    for (unsigned i = 0; i < HK_CHILD_SLOTS && result < 0 &&
        !*storage_failed; ++i)
    {
        unsigned slot = (start + i) & (HK_CHILD_SLOTS - 1);
        if (!children[slot].id)
        {
            if (create)
            {
                children[slot].id = hk_id_arena_copy(identifiers, id);
                if (!children[slot].id) *storage_failed = 1;
                else
                {
                    children[slot].session = session;
                    result = (int)slot;
                }
            }
            else i = HK_CHILD_SLOTS;
        }
        else if (children[slot].session == session && cm_equal(children[slot].id, id))
            result = (int)slot;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int hk_process_trace_event(const CmJson *json,
    unsigned event, size_t source_offset, unsigned line, unsigned event_index,
    HkSession *sessions, HkChild *children, HkReport *report,
    unsigned *session_count, HkIdArena *identifiers)
{
    unsigned event_field = cm_member(json, event, "event");
    unsigned sid_field = cm_member(json, event, "sid");
    const char *name = cm_get(json, event, "event");
    const char *sid = cm_get(json, event, "sid");
    const char *child_id = "";
    unsigned child_field = cm_member(json, event, "child_id");
    uint64_t child_number = 0;
    int sid_is_string = sid_field && json->tokens[sid_field].kind == 's';
    int child_id_valid = child_field && json->tokens[child_field].kind == 'v' &&
        cm_unsigned(cm_value(json, child_field), &child_number);
    if (child_id_valid)
    {
        child_id = cm_value(json, child_field);
    }
    HkLocation event_location = {0};
    hk_location_set(&event_location, source_offset, line, event_index,
        hk_event_label(name), sid_is_string ? sid : "", child_id_valid ? child_id : "");
    report->events_seen = event_index;
    int slot = -1, storage_failed = 0;
    int valid = json->tokens[event].kind == 'o' && event_field && sid_field &&
        json->tokens[event_field].kind == 's' && sid_is_string && sid[0] != 0;
    if (!valid) hk_report_failure(report, "event", "event_shape", &event_location);
    if (valid)
    {
        slot = hk_session_slot(sessions, sid, 1, session_count,
            identifiers, &storage_failed);
        report->sessions = *session_count;
        valid = slot >= 0;
        if (!valid) hk_report_failure(report, "session",
            storage_failed ? "identifier_storage_failed" : "session_capacity",
            &event_location);
    }
    if (valid)
    {
        HkSession *session = &sessions[(unsigned)slot];
        valid = session->atexit == 0;
        if (!valid) hk_report_failure(report, "event", "after_atexit", &event_location);
        if (valid)
        {
            ++report->events;
            if (cm_equal(name, "version"))
            {
                unsigned exe = cm_member(json, event, "exe");
                int sequence_ok = session->version == 0 && session->start == 0;
                valid = sequence_ok && exe && json->tokens[exe].kind == 's' &&
                    cm_get(json, event, "exe")[0] != 0;
                if (valid) ++session->version;
                else hk_report_failure(report, "event",
                    sequence_ok ? "version_shape" : "version_sequence", &event_location);
            }
            else if (cm_equal(name, "start"))
            {
                unsigned argv = cm_member(json, event, "argv");
                int sequence_ok = session->version == 1 && session->start == 0;
                valid = sequence_ok && hk_auto_argv(json, argv, report, &event_location);
                if (valid) ++session->start;
                else hk_report_failure(report, "event",
                    sequence_ok ? "invalid_argv" : "start_sequence", &event_location);
            }
            else if (cm_equal(name, "exit"))
            {
                valid = session->start == 1 && session->exit == 0;
                if (valid) ++session->exit;
                else hk_report_failure(report, "event", "exit_sequence", &event_location);
            }
            else if (cm_equal(name, "atexit"))
            {
                valid = session->exit == 1 && session->atexit == 0;
                if (valid) ++session->atexit;
                else hk_report_failure(report, "event", "atexit_sequence", &event_location);
            }
            else if (cm_equal(name, "child_start"))
            {
                int order_ok = session->start == 1 && session->exit == 0;
                unsigned auto_before = report->automatic;
                valid = order_ok && child_id_valid &&
                    hk_auto_argv(json, cm_member(json, event, "argv"),
                        report, &event_location);
                if (!valid)
                {
                    const char *reason = !order_ok ? "child_start_sequence" :
                        !child_id_valid ? "child_id_invalid" : "invalid_argv";
                    hk_report_failure(report, "event", reason, &event_location);
                }
                if (valid && report->automatic > auto_before) ++report->automatic_children;
                if (valid)
                {
                    const char *id = cm_value(json, child_field);
                    int child_slot = hk_child_slot(children, (unsigned)slot,
                        id, 1, identifiers, &storage_failed);
                    valid = child_slot >= 0 && !children[(unsigned)child_slot].started;
                    if (valid)
                    {
                        children[(unsigned)child_slot].started = 1;
                        children[(unsigned)child_slot].start_event = event_location;
                        ++report->child_starts;
                    }
                    else hk_report_failure(report, "child",
                        child_slot < 0 ? (storage_failed ? "identifier_storage_failed" :
                            "child_capacity") : "duplicate_child_start",
                        &event_location);
                }
            }
            else if (cm_equal(name, "child_exit"))
            {
                int order_ok = session->start == 1 && session->exit == 0;
                valid = order_ok && child_id_valid;
                if (!valid) hk_report_failure(report, "event",
                    order_ok ? "child_id_invalid" : "child_exit_sequence", &event_location);
                if (valid)
                {
                    int child_slot = hk_child_slot(children, (unsigned)slot,
                        cm_value(json, child_field), 0, identifiers, &storage_failed);
                    valid = child_slot >= 0 && children[(unsigned)child_slot].started &&
                        !children[(unsigned)child_slot].exited;
                    if (valid)
                    {
                        children[(unsigned)child_slot].exited = 1;
                        ++report->child_exits;
                    }
                    else hk_report_failure(report, "child",
                        child_slot < 0 ? "child_exit_unmatched" :
                            children[(unsigned)child_slot].exited ?
                            "duplicate_child_exit" : "child_exit_before_start",
                        &event_location);
                }
            }
        }
    }
    return valid;
}

// Parse every bounded LF-delimited record with reusable JSON scratch. Session and
// child identifiers live in a separate trace-sized arena through lifecycle proof.
BUSTER_GLOBAL_LOCAL int hk_parse_trace_data(const char *bytes, size_t length,
    HkReport *out)
{
    HkSession *sessions = NULL;
    HkChild *children = NULL;
    HkIdArena identifiers = {0};
    CmJson json = {0};
    HkReport report = {0};
    size_t line_start = 0;
    unsigned session_count = 0, event_index = 0, source_line = 1;
    int valid = bytes != NULL && length > 0 && length <= HK_TRACE_LIMIT;
    report.trace_bytes = length;
    if (!bytes) hk_report_failure(&report, "input", "missing_bytes", NULL);
    else if (!length) hk_report_failure(&report, "input", "empty_trace", NULL);
    else if (length > HK_TRACE_LIMIT)
    {
        HkLocation location = {0};
        hk_location_set(&location, HK_TRACE_LIMIT, 1, 0, "", "", "");
        hk_report_failure(&report, "input", "trace_limit_exceeded", &location);
    }
    else if (bytes[length - 1] != '\n')
    {
        unsigned line = 1;
        for (size_t i = 0; i < length; ++i) if (bytes[i] == '\n') ++line;
        HkLocation location = {0};
        hk_location_set(&location, length, line, 0, "end_of_trace", "", "");
        hk_report_failure(&report, "input", "missing_final_newline", &location);
    }
    valid = valid && bytes[length - 1] == '\n';
    if (valid)
    {
        sessions = calloc(HK_SESSION_SLOTS, sizeof(*sessions));
        children = calloc(HK_CHILD_SLOTS, sizeof(*children));
        identifiers.capacity = length + 1;
        identifiers.bytes = malloc(identifiers.capacity);
        valid = sessions && children && identifiers.bytes;
        if (!valid) hk_report_failure(&report, "memory", "allocation_failed", NULL);
    }
    while (valid && line_start < length)
    {
        size_t line_end = line_start;
        while (line_end < length && bytes[line_end] != '\n' &&
            bytes[line_end] != '\0' && bytes[line_end] != '\r') ++line_end;
        if (line_end < length && (bytes[line_end] == '\0' || bytes[line_end] == '\r'))
        {
            HkLocation location = {0};
            hk_location_set(&location, line_end, source_line, event_index + 1,
                "record", "", "");
            hk_report_failure(&report, "record", "forbidden_byte", &location);
            valid = 0;
        }
        else if (line_end >= length)
        {
            HkLocation location = {0};
            hk_location_set(&location, length, source_line, event_index + 1,
                "end_of_trace", "", "");
            hk_report_failure(&report, "record", "incomplete_record", &location);
            valid = 0;
        }
        else if (line_end == line_start)
        {
            HkLocation location = {0};
            hk_location_set(&location, line_start, source_line, event_index + 1,
                "record", "", "");
            hk_report_failure(&report, "record", "empty_line", &location);
            valid = 0;
        }
        else
        {
            size_t record_length = line_end - line_start;
            unsigned token_limit = (unsigned)record_length + 2u;
            if (token_limit > CM_TOKENS) token_limit = CM_TOKENS;
            cm_json_parse_reuse(&json, bytes + line_start,
                record_length, token_limit);
            HkLocation location = {0};
            hk_location_set(&location, line_start, source_line, event_index + 1,
                "record_start", "", "");
            if (json.count > report.json_tokens) report.json_tokens = json.count;
            if (json.used > report.json_arena_bytes) report.json_arena_bytes = json.used;
            valid = json.valid && json.count > 0 && json.tokens[1].kind == 'o';
            if (!valid)
            {
                if (!json.valid) hk_report_failure(&report, "json", "parse_failed", &location);
                else hk_report_failure(&report, "json", "root_not_object", &location);
            }
            if (valid)
            {
                ++event_index;
                valid = hk_process_trace_event(&json, 1, line_start, source_line,
                    event_index, sessions, children, &report, &session_count,
                    &identifiers);
            }
            line_start = line_end + 1;
            ++source_line;
        }
    }
    if (valid && line_start != length)
    {
        HkLocation location = {0};
        hk_location_set(&location, line_start, source_line, event_index + 1,
            "end_of_trace", "", "");
        hk_report_failure(&report, "record", "unconsumed_bytes", &location);
        valid = 0;
    }
    if (valid)
    {
        report.sessions = session_count;
        valid = session_count > 0;
        if (!valid)
        {
            HkLocation location = {0};
            hk_location_set(&location, length, source_line, event_index + 1,
                "end_of_trace", "", "");
            hk_report_failure(&report, "lifecycle", "no_sessions", &location);
        }
        for (unsigned i = 0; valid && i < HK_SESSION_SLOTS; ++i)
        {
            if (sessions[i].sid)
            {
                int complete = sessions[i].version == 1 && sessions[i].start == 1 &&
                    sessions[i].exit == 1 && sessions[i].atexit == 1;
                if (!complete)
                {
                    HkLocation location = {0};
                    hk_location_set(&location, length, source_line, event_index + 1,
                        "end_of_trace", sessions[i].sid, "");
                    hk_report_failure(&report, "lifecycle", "incomplete_session", &location);
                }
                valid = valid && complete;
            }
        }
        for (unsigned i = 0; valid && i < HK_CHILD_SLOTS; ++i)
        {
            if (children[i].id)
            {
                int complete = children[i].started == 1 && children[i].exited == 1;
                if (!complete) hk_report_failure(&report, "lifecycle", "incomplete_child",
                    &children[i].start_event);
                valid = valid && complete;
            }
        }
        if (valid && report.child_starts != report.child_exits)
        {
            hk_report_failure(&report, "lifecycle", "child_count_mismatch", NULL);
            valid = 0;
        }
    }
    report.sessions = session_count;
    for (unsigned i = 0; i < HK_SESSION_SLOTS; ++i)
    {
        if (sessions && sessions[i].sid)
        {
            int complete = sessions[i].version == 1 && sessions[i].start == 1 &&
                sessions[i].exit == 1 && sessions[i].atexit == 1;
            if (complete) ++report.sessions_complete;
            else ++report.sessions_incomplete;
        }
    }
    for (unsigned i = 0; i < HK_CHILD_SLOTS; ++i)
    {
        if (children && children[i].id)
        {
            if (children[i].started == 1 && children[i].exited == 1)
                ++report.children_complete;
            else ++report.children_incomplete;
        }
    }
    report.parser_valid = valid;
    report.complete = valid;
    if (out) *out = report;
    cm_json_free(&json);
    free(identifiers.bytes);
    free(children);
    free(sessions);
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_parse_trace_file(const char *path, HkReport *report)
{
    FILE *file = NULL;
    char *bytes = NULL;
    size_t length = 0;
    int valid = 1, read_error = 0, reached_eof = 0;
    HkReport parsed = {0};
    errno = 0;
    file = fopen(path, "rb");
    if (!file)
    {
        int error_number = errno;
        hk_report_failure(&parsed, "file", "open_failed", NULL);
        parsed.failure_event = 0;
        valid = 0;
        parsed.io_error_number = error_number;
    }
    if (valid)
    {
        errno = 0;
        bytes = malloc(HK_TRACE_LIMIT + 1u);
        if (!bytes)
        {
            hk_report_failure(&parsed, "file", "allocation_failed", NULL);
            parsed.io_error_number = errno;
            valid = 0;
        }
    }
    if (valid)
    {
        errno = 0;
        length = fread(bytes, 1, HK_TRACE_LIMIT, file);
        parsed.trace_bytes = length;
        read_error = ferror(file);
        reached_eof = feof(file);
        valid = !read_error && reached_eof && length > 0;
        if (!valid)
        {
            if (read_error)
            {
                hk_report_failure(&parsed, "file", "read_failed", NULL);
                parsed.io_error_number = errno;
            }
            else if (!reached_eof) hk_report_failure(&parsed, "file", "trace_limit_reached", NULL);
            else hk_report_failure(&parsed, "file", "empty_trace", NULL);
        }
    }
    if (file)
    {
        errno = 0;
        if (fclose(file) != 0)
        {
            if (valid) hk_report_failure(&parsed, "file", "close_failed", NULL);
            parsed.io_error_number = errno;
            valid = 0;
        }
    }
    if (valid) valid = hk_parse_trace_data(bytes, length, &parsed);
    free(bytes);
    if (report) *report = parsed;
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_append_trace_record(char *buffer,
    size_t capacity, size_t *used, const char *format, const char *prefix,
    unsigned session)
{
    int written = snprintf(buffer + *used, capacity - *used, format, prefix, session);
    int valid = written >= 0 && (size_t)written < capacity - *used;
    if (valid) *used += (size_t)written;
    return valid;
}

BUSTER_GLOBAL_LOCAL int hk_large_trace_control(void)
{
    enum { HK_FIXTURE_SESSIONS = 5000u, HK_TOKENS_PER_SESSION = 30u };
    char *bytes = malloc(HK_TRACE_LIMIT);
    const char *sid_prefix =
        "team/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/";
    size_t length = 0;
    HkReport report = {0};
    int valid = bytes != NULL;
    for (unsigned session = 0; valid && session < HK_FIXTURE_SESSIONS; ++session)
    {
        valid = hk_append_trace_record(bytes, HK_TRACE_LIMIT, &length,
            "{\"event\":\"version\",\"sid\":\"%s%u\",\"exe\":\"git\"}\n", sid_prefix, session);
        valid = valid && hk_append_trace_record(bytes, HK_TRACE_LIMIT, &length,
            "{\"event\":\"start\",\"sid\":\"%s%u\",\"argv\":[\"git\",\"version\"]}\n", sid_prefix, session);
        valid = valid && hk_append_trace_record(bytes, HK_TRACE_LIMIT, &length,
            "{\"event\":\"exit\",\"sid\":\"%s%u\",\"code\":0}\n", sid_prefix, session);
        valid = valid && hk_append_trace_record(bytes, HK_TRACE_LIMIT, &length,
            "{\"event\":\"atexit\",\"sid\":\"%s%u\",\"code\":0}\n", sid_prefix, session);
    }
    valid = valid && (uint64_t)HK_FIXTURE_SESSIONS * HK_TOKENS_PER_SESSION > CM_TOKENS;
    valid = valid && hk_parse_trace_data(bytes, length, &report);
    valid = valid && report.complete && report.parser_valid &&
        report.trace_bytes == length && report.sessions == HK_FIXTURE_SESSIONS &&
        report.sessions_complete == HK_FIXTURE_SESSIONS &&
        report.events == HK_FIXTURE_SESSIONS * 4u &&
        report.events_seen == HK_FIXTURE_SESSIONS * 4u &&
        report.json_tokens < CM_TOKENS;
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
    const char *complete_child =
        "{\"event\":\"version\",\"sid\":\"s7\",\"exe\":\"git\"}\n"
        "{\"event\":\"start\",\"sid\":\"s7\",\"argv\":[\"git\",\"commit\"]}\n"
        "{\"event\":\"child_start\",\"sid\":\"s7\",\"child_id\":7,\"argv\":[\"git\",\"status\"]}\n"
        "{\"event\":\"child_exit\",\"sid\":\"s7\",\"child_id\":7}\n"
        "{\"event\":\"exit\",\"sid\":\"s7\",\"code\":0}\n"
        "{\"event\":\"atexit\",\"sid\":\"s7\",\"code\":0}\n";
    const char *unmatched_child_exit =
        "{\"event\":\"version\",\"sid\":\"s8\",\"exe\":\"git\"}\n"
        "{\"event\":\"start\",\"sid\":\"s8\",\"argv\":[\"git\",\"commit\"]}\n"
        "{\"event\":\"child_exit\",\"sid\":\"s8\",\"child_id\":8}\n"
        "{\"event\":\"exit\",\"sid\":\"s8\",\"code\":0}\n"
        "{\"event\":\"atexit\",\"sid\":\"s8\",\"code\":0}\n";
    const char *malformed_middle =
        "{\"event\":\"version\",\"sid\":\"s9\",\"exe\":\"git\"}\n"
        "{\"event\":\n"
        "{\"event\":\"start\",\"sid\":\"s9\",\"argv\":[\"git\",\"version\"]}\n"
        "{\"event\":\"exit\",\"sid\":\"s9\",\"code\":0}\n"
        "{\"event\":\"atexit\",\"sid\":\"s9\",\"code\":0}\n";
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
        report.sessions == 1 && report.events == 4 && report.sessions_complete == 1 &&
        report.sessions_incomplete == 0 && report.complete;
    valid = valid && !hk_parse_trace_data(missing_end, strlen(missing_end), &report);
    valid = valid && cm_equal(report.failure_phase, "lifecycle") &&
        cm_equal(report.failure_reason, "incomplete_session") &&
        report.sessions == 1 && report.events_seen == 3 &&
        report.sessions_incomplete == 1 && !report.complete &&
        cm_equal(report.failure_event_type, "end_of_trace") &&
        cm_equal(report.failure_sid, "s2");
    valid = valid && !hk_parse_trace_data(incomplete_child, strlen(incomplete_child), &report);
    valid = valid && cm_equal(report.failure_phase, "lifecycle") &&
        cm_equal(report.failure_reason, "incomplete_child") &&
        report.children_incomplete == 1 &&
        cm_equal(report.failure_event_type, "child_start") &&
        cm_equal(report.failure_sid, "s3") &&
        cm_equal(report.failure_child_id, "1");
    valid = valid && !hk_parse_trace_data(wrong_argv_type, strlen(wrong_argv_type), &report);
    valid = valid && cm_equal(report.failure_phase, "event") &&
        cm_equal(report.failure_reason, "invalid_argv") &&
        report.failure_line == 2 && report.failure_event == 2 &&
        cm_equal(report.failure_event_type, "start") &&
        cm_equal(report.failure_sid, "s4");
    valid = valid && !hk_parse_trace_data(empty_argv, strlen(empty_argv), &report);
    valid = valid && !hk_parse_trace_data(empty_argv0, strlen(empty_argv0), &report);
    valid = valid && hk_parse_trace_data(complete_child, strlen(complete_child), &report) &&
        report.child_starts == 1 && report.child_exits == 1 &&
        report.children_complete == 1 && report.children_incomplete == 0;
    valid = valid && !hk_parse_trace_data(unmatched_child_exit,
        strlen(unmatched_child_exit), &report);
    valid = valid && cm_equal(report.failure_phase, "child") &&
        cm_equal(report.failure_reason, "child_exit_unmatched") &&
        report.failure_line == 3 && report.failure_event == 3 &&
        cm_equal(report.failure_sid, "s8") &&
        cm_equal(report.failure_child_id, "8");
    valid = valid && !hk_parse_trace_data(complete, strlen(complete) - 1, &report);
    valid = valid && cm_equal(report.failure_phase, "input") &&
        cm_equal(report.failure_reason, "missing_final_newline") &&
        report.failure_offset_known && report.failure_offset == strlen(complete) - 1 &&
        report.failure_line == 4;
    valid = valid && !hk_parse_trace_data(malformed, strlen(malformed), &report);
    valid = valid && cm_equal(report.failure_phase, "json") &&
        cm_equal(report.failure_reason, "parse_failed") &&
        report.failure_offset_known && report.failure_offset == 0 &&
        report.failure_line == 1 && report.failure_event == 1 &&
        cm_equal(report.failure_event_type, "record_start") && report.json_tokens > 0;
    valid = valid && !hk_parse_trace_data(malformed_middle, strlen(malformed_middle), &report);
    valid = valid && cm_equal(report.failure_phase, "json") &&
        cm_equal(report.failure_reason, "parse_failed") &&
        report.failure_offset_known &&
        report.failure_offset == strlen("{\"event\":\"version\",\"sid\":\"s9\",\"exe\":\"git\"}\n") &&
        report.failure_line == 2 &&
        cm_equal(report.failure_event_type, "record_start");
    valid = valid && hk_large_trace_control();
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
    HkRun run = {0};
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

BUSTER_GLOBAL_LOCAL int hk_directory_empty(const char *path,
    HkDirectoryReport *out)
{
    DIR *directory = NULL;
    struct dirent *entry = NULL;
    HkDirectoryReport report = {0};
    int valid = 1;
    errno = 0;
    directory = opendir(path);
    if (!directory)
    {
        report.error_number = errno;
        valid = 0;
    }
    errno = 0;
    while (valid && (entry = readdir(directory)) != NULL)
    {
        if (cm_equal(entry->d_name, ".") || cm_equal(entry->d_name, "..")) continue;
        report.entries = 1;
        valid = 0;
    }
    if (directory && errno != 0)
    {
        report.error_number = errno;
        valid = 0;
    }
    if (directory)
    {
        errno = 0;
        if (closedir(directory) != 0)
        {
            report.close_error_number = errno;
            valid = 0;
        }
    }
    if (out) *out = report;
    return valid && report.entries == 0;
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
    HkRun run = {0};
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

BUSTER_GLOBAL_LOCAL void hk_print_protected_diagnostic(int run_attempted,
    int runner_valid, const HkRun *run, int scratch_checked, int scratch_empty,
    const HkDirectoryReport *scratch, int trace_checked, int proof_checked,
    int proof_valid, const HkReport *report)
{
    const char *scratch_state = !scratch_checked ? "not_run" :
        scratch->error_number || scratch->close_error_number ? "error" :
        scratch_empty ? "empty" : "nonempty";
    const char *trace_state = !trace_checked ? "not_run" :
        report->parser_valid ? "complete" : "failed";
    const char *proof_state = !proof_checked ? "not_run" : proof_valid ? "pass" : "fail";
    fprintf(stderr,
        "Protected diagnostics: run=%s runner_ok=%d spawned=%d reaped=%d timeout=%d exit=%d signal=%d group_gone=%d run_errno=%d; scratch=%s entries_at_least=%u errno=%d close_errno=%d; trace=%s proof=%s bytes=%zu record_tokens_peak=%u/%u record_arena_bytes=%u events=%u/%u sessions=%u complete=%u incomplete=%u children=%u complete=%u incomplete=%u starts=%u exits=%u automatic=%u automatic_children=%u detached=%u; first=%s/%s line=%u offset=",
        !run_attempted ? "not_run" : runner_valid && !run->timed_out && run->code == 0 ? "passed" : "failed",
        runner_valid, run->spawned, run->reaped, run->timed_out, run->code,
        run->signal_number, run->group_gone, run->error_number, scratch_state,
        scratch->entries, scratch->error_number, scratch->close_error_number,
        trace_state, proof_state, report->trace_bytes, report->json_tokens, CM_TOKENS,
        report->json_arena_bytes, report->events, report->events_seen, report->sessions,
        report->sessions_complete, report->sessions_incomplete,
        report->children_complete + report->children_incomplete,
        report->children_complete, report->children_incomplete,
        report->child_starts, report->child_exits, report->automatic,
        report->automatic_children, report->detached,
        report->failure_phase[0] ? report->failure_phase : "none",
        report->failure_reason[0] ? report->failure_reason : "none",
        report->failure_line);
    if (report->failure_offset_known) fprintf(stderr, "%zu", report->failure_offset);
    else fputs("unknown", stderr);
    fprintf(stderr, " event=%u type=%s sid=%s child_id=%s io_errno=%d\n",
        report->failure_event,
        report->failure_event_type[0] ? report->failure_event_type : "none",
        report->failure_sid[0] ? report->failure_sid : "none",
        report->failure_child_id[0] ? report->failure_child_id : "none",
        report->io_error_number);
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
    HkRun timeout = {0}, protected_run = {0};
    HkDirectoryReport scratch_report = {0};
    char *sleeping[] = {"sleep", "2", NULL};
    int valid = hk_trace_parser_controls();
    int have_root = 0;
    int protected_run_attempted = 0, protected_runner_valid = 0;
    int scratch_checked = 0, scratch_empty = 0, trace_checked = 0;
    int proof_checked = 0, proof_valid = 0;
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
        protected_run_attempted = 1;
        protected_runner_valid = hk_run(python, ".", HK_GUARDED, protected_trace,
            temporary, root, 300000u, &protected_run);
        int protected_valid = protected_runner_valid;
        protected_valid = protected_valid && !protected_run.timed_out &&
            protected_run.code == 0;
        if (!protected_valid)
        {
            const char *reason = !protected_runner_valid ? "runner_failure" :
                protected_run.timed_out ? "timed_out" : "nonzero_exit";
            hk_report_failure(&guarded, "process", reason, NULL);
        }
        if (protected_valid)
        {
            scratch_checked = 1;
            scratch_empty = hk_directory_empty(temporary, &scratch_report);
            protected_valid = protected_valid && scratch_empty;
            if (!protected_valid)
                hk_report_failure(&guarded, "scratch",
                    scratch_report.error_number || scratch_report.close_error_number ?
                    "directory_check_failed" : "directory_not_empty", NULL);
        }
        if (protected_valid)
        {
            trace_checked = 1;
            protected_valid = hk_parse_trace_file(protected_trace, &guarded);
        }
        if (protected_valid)
        {
            proof_checked = 1;
            proof_valid = guarded.sessions > 0 && guarded.automatic == 0 &&
                guarded.detached == 0 && guarded.child_starts == guarded.child_exits;
            protected_valid = protected_valid && proof_valid;
            if (!proof_valid)
            {
                if (guarded.automatic > 0)
                    hk_report_failure(&guarded, "proof", "automatic_housekeeping",
                        &guarded.first_automatic);
                else if (guarded.detached > 0)
                    hk_report_failure(&guarded, "proof", "detached_housekeeping",
                        &guarded.first_detached);
                else if (guarded.child_starts != guarded.child_exits)
                    hk_report_failure(&guarded, "proof", "child_count_mismatch", NULL);
                else
                    hk_report_failure(&guarded, "proof", "empty_session_set", NULL);
            }
        }
        if (!protected_valid)
        {
            hk_print_protected_diagnostic(protected_run_attempted,
                protected_runner_valid, &protected_run, scratch_checked, scratch_empty,
                &scratch_report, trace_checked, proof_checked, proof_valid, &guarded);
            fprintf(stderr, "Protected suite did not produce a complete, housekeeping-free Trace2 record.\n");
        }
        valid = valid && protected_valid;
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
