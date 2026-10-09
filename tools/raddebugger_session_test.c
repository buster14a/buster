#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * First-party C supervisor for the pinned RAD Debugger session oracle.
 *
 * Build with a trusted host C compiler and -lX11.  It does not modify or
 * instrument RAD Debugger.  The GUI starts with a temporary target but without
 * --auto_run; the first IPC run-to command installs the stop condition before
 * the debuggee is allowed to execute.
 */

#define MAX_IPC_BYTES (1024u * 1024u)
#define MAX_VALUE_BYTES 4096u
#define DEFAULT_TIMEOUT_MS 120000u
#define POLL_INTERVAL_MS 50
#define MAX_X11_DEPTH 16u
#define MAX_X11_NODES 4096u
#define MAX_THREADS 64u

typedef struct Args Args;
struct Args
{
    const char *raddbg;
    const char *debuggee;
    const char *source;
    const char *session_dir;
    uint16_t port;
    uint32_t timeout_ms;
    int self_test;
};

typedef struct Buffer Buffer;
struct Buffer
{
    char *data;
    size_t size;
    size_t capacity;
};

typedef struct State State;
struct State
{
    int running;
    uint64_t run_gen;
    uint64_t stop_count;
    uint64_t ip;
    char module[MAX_VALUE_BYTES];
    char symbol[MAX_VALUE_BYTES];
    size_t thread_count;
    uint64_t first_thread_id;
    uint64_t thread_ids[MAX_THREADS];
    uint64_t thread_ips[MAX_THREADS];
};

typedef struct SourceLines SourceLines;
struct SourceLines
{
    unsigned outer;
    unsigned inner;
    unsigned inner_done;
    unsigned worker;
};

typedef struct EvalResult EvalResult;
struct EvalResult
{
    char expr[MAX_VALUE_BYTES];
    char value[MAX_VALUE_BYTES];
    char type[MAX_VALUE_BYTES];
    char messages[MAX_VALUE_BYTES];
};

typedef struct ExpectedValue ExpectedValue;
struct ExpectedValue
{
    const char *expr;
    const char *value;
};

typedef struct Session Session;
struct Session
{
    Args args;
    char temp_dir[PATH_MAX];
    char user_path[PATH_MAX];
    char project_path[PATH_MAX];
    char logs_path[PATH_MAX];
    char port_arg[64];
    pid_t gui_pid;
    pid_t gui_group;
    pid_t target_pid;
    Display *display;
    int gui_output_fd;
    Buffer gui_output;
    uint64_t deadline_ms;
    int failed;
};

static volatile sig_atomic_t g_interrupted;
static FILE *g_log;

static int drain_gui_output(Session *session);

static void
signal_handler(int signo)
{
    (void)signo;
    g_interrupted = 1;
}

static uint64_t
monotonic_ms(void)
{
    struct timespec now = {0};
    uint64_t result = 0;
    if(clock_gettime(CLOCK_MONOTONIC, &now) == 0)
    {
        result = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
    }
    return result;
}

static void
log_text(const char *label, const char *text)
{
    FILE *stream = g_log != NULL ? g_log : stdout;
    fprintf(stream, "%s\n", label);
    if(text != NULL && text[0] != 0)
    {
        fprintf(stream, "%s", text);
        if(text[strlen(text) - 1] != '\n')
        {
            fputc('\n', stream);
        }
    }
    fflush(stream);
}

static int
buffer_append(Buffer *buffer, const char *data, size_t size)
{
    int ok = 0;
    if(buffer->size <= MAX_IPC_BYTES && size <= MAX_IPC_BYTES - buffer->size)
    {
        size_t need = buffer->size + size + 1;
        if(need <= MAX_IPC_BYTES + 1)
        {
            if(need > buffer->capacity)
            {
                size_t next_capacity = buffer->capacity != 0 ? buffer->capacity : 4096;
                while(next_capacity < need && next_capacity < MAX_IPC_BYTES + 1)
                {
                    next_capacity *= 2;
                    if(next_capacity > MAX_IPC_BYTES + 1)
                    {
                        next_capacity = MAX_IPC_BYTES + 1;
                    }
                }
                char *next = realloc(buffer->data, next_capacity);
                if(next != NULL)
                {
                    buffer->data = next;
                    buffer->capacity = next_capacity;
                }
            }
            if(need <= buffer->capacity)
            {
                memcpy(buffer->data + buffer->size, data, size);
                buffer->size += size;
                buffer->data[buffer->size] = 0;
                ok = 1;
            }
        }
    }
    return ok;
}

static int
parse_u64(const char *text, uint64_t *value_out)
{
    int ok = 0;
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 0);
    while(end != NULL && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
    {
        end += 1;
    }
    if(errno == 0 && end != text && end != NULL && *end == 0)
    {
        *value_out = (uint64_t)value;
        ok = 1;
    }
    return ok;
}

static int
copy_decoded_value(const char *src, char *dst, size_t dst_cap)
{
    int ok = 0;
    size_t out = 0;
    size_t len = strlen(src);
    const char *p = src;
    while(*p == ' ' || *p == '\t')
    {
        p += 1;
    }
    len = strlen(p);
    if(len >= 2 && p[0] == '"' && p[len - 1] == '"')
    {
        p += 1;
        len -= 2;
        for(size_t i = 0; i < len && out + 1 < dst_cap; i += 1)
        {
            char c = p[i];
            if(c == '\\' && i + 1 < len)
            {
                i += 1;
                switch(p[i])
                {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case '\\': c = '\\'; break;
                    case '"': c = '"'; break;
                    default: c = p[i]; break;
                }
            }
            dst[out++] = c;
        }
        if(out + 1 < dst_cap)
        {
            dst[out] = 0;
            ok = 1;
        }
    }
    else
    {
        while(len != 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r' || p[len - 1] == '\n'))
        {
            len -= 1;
        }
        if(len < dst_cap)
        {
            memcpy(dst, p, len);
            dst[len] = 0;
            ok = 1;
        }
    }
    return ok;
}

/* Reads exactly one scalar field at a fixed indentation; missing or duplicate
 * fields are rejected.  Values with quotes are unescaped into dst. */
static int
read_field(const char *text, unsigned indent, const char *key, char *dst, size_t dst_cap)
{
    int count = 0;
    int ok = 0;
    size_t key_len = strlen(key);
    const char *line = text;
    while(line != NULL && *line != 0)
    {
        const char *end = strchr(line, '\n');
        size_t line_len = end != NULL ? (size_t)(end - line) : strlen(line);
        if(line_len > indent + key_len && line_len >= indent &&
           strncmp(line, "                ", indent) == 0 &&
           strncmp(line + indent, key, key_len) == 0 &&
           line[indent + key_len] == ':')
        {
            const char *value = line + indent + key_len + 1;
            size_t value_len = line_len - (size_t)(value - line);
            char temporary[MAX_VALUE_BYTES];
            if(value_len < sizeof(temporary))
            {
                memcpy(temporary, value, value_len);
                temporary[value_len] = 0;
                count += 1;
                if(count == 1 && copy_decoded_value(temporary, dst, dst_cap))
                {
                    ok = 1;
                }
            }
            else
            {
                count += 1;
                ok = 0;
            }
        }
        line = end != NULL ? end + 1 : NULL;
    }
    if(count != 1)
    {
        ok = 0;
    }
    return ok;
}

static int
line_key_count(const char *text, unsigned indent, const char *key)
{
    int count = 0;
    size_t key_len = strlen(key);
    const char *line = text;
    while(line != NULL && *line != 0)
    {
        const char *end = strchr(line, '\n');
        size_t line_len = end != NULL ? (size_t)(end - line) : strlen(line);
        if(line_len > indent + key_len && line_len >= indent &&
           strncmp(line, "                ", indent) == 0 &&
           strncmp(line + indent, key, key_len) == 0 && line[indent + key_len] == ':')
        {
            count += 1;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return count;
}

static int
balanced_md(const char *text)
{
    int ok = 1;
    int in_string = 0;
    int escaped = 0;
    int depth = 0;
    for(const char *p = text; *p != 0; p += 1)
    {
        if(in_string)
        {
            if(escaped)
            {
                escaped = 0;
            }
            else if(*p == '\\')
            {
                escaped = 1;
            }
            else if(*p == '"')
            {
                in_string = 0;
            }
        }
        else if(*p == '"')
        {
            in_string = 1;
        }
        else if(*p == '{')
        {
            depth += 1;
        }
        else if(*p == '}')
        {
            depth -= 1;
            if(depth < 0)
            {
                ok = 0;
            }
        }
    }
    if(depth != 0 || in_string)
    {
        ok = 0;
    }
    return ok;
}

static int
parse_state(const char *text, State *state)
{
    int ok = 0;
    char number[MAX_VALUE_BYTES];
    uint64_t running = 0;
    if(text != NULL && strlen(text) <= MAX_IPC_BYTES && strncmp(text, "state:", 6) == 0 && balanced_md(text) &&
       line_key_count(text, 1, "stop_event") == 1 && line_key_count(text, 1, "locals") == 1 &&
       line_key_count(text, 1, "lines") == 1 && line_key_count(text, 1, "threads") == 1 &&
       line_key_count(text, 1, "modules") == 1)
    {
        State parsed = {0};
        if(read_field(text, 1, "running", number, sizeof(number)) && parse_u64(number, &running) && running <= 1 &&
           read_field(text, 1, "run_gen", number, sizeof(number)) && parse_u64(number, &parsed.run_gen) &&
           read_field(text, 1, "stop_count", number, sizeof(number)) && parse_u64(number, &parsed.stop_count) &&
           read_field(text, 1, "ip", number, sizeof(number)) && parse_u64(number, &parsed.ip) &&
           read_field(text, 1, "ip_module", parsed.module, sizeof(parsed.module)) &&
           read_field(text, 1, "ip_voff_symbol", parsed.symbol, sizeof(parsed.symbol)))
        {
            parsed.running = running != 0;
            const char *threads = strstr(text, " threads:\n");
            const char *modules = strstr(text, " modules:\n");
            if(threads != NULL && modules != NULL && modules > threads)
            {
                const char *p = threads;
                int in_thread = 0;
                int name_count = 0;
                int id_count = 0;
                int ip_count = 0;
                uint64_t thread_id = 0;
                uint64_t thread_ip = 0;
                while(p != NULL && p < modules)
                {
                    const char *end = strchr(p, '\n');
                    size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
                    if(len == 3 && strncmp(p, "  {", 3) == 0)
                    {
                        if(in_thread || parsed.thread_count >= MAX_THREADS)
                        {
                            parsed.thread_count = 0;
                            parsed.first_thread_id = 0;
                            break;
                        }
                        in_thread = 1;
                        name_count = 0;
                        id_count = 0;
                        ip_count = 0;
                        thread_id = 0;
                        thread_ip = 0;
                    }
                    else if(in_thread && len >= 6 && strncmp(p, "   id:", 6) == 0)
                    {
                        char value[MAX_VALUE_BYTES];
                        size_t value_len = len - 6;
                        if(value_len < sizeof(value))
                        {
                            memcpy(value, p + 6, value_len);
                            value[value_len] = 0;
                            if(parse_u64(value, &thread_id) && id_count == 0)
                            {
                                id_count += 1;
                            }
                            else
                            {
                                id_count += 2;
                            }
                        }
                        else
                        {
                            id_count += 2;
                        }
                    }
                    else if(in_thread && len >= 8 && strncmp(p, "   name:", 8) == 0)
                    {
                        name_count += 1;
                    }
                    else if(in_thread && len >= 6 && strncmp(p, "   ip:", 6) == 0)
                    {
                        char value[MAX_VALUE_BYTES];
                        size_t value_len = len - 6;
                        if(value_len < sizeof(value))
                        {
                            memcpy(value, p + 6, value_len);
                            value[value_len] = 0;
                            if(parse_u64(value, &thread_ip) && ip_count == 0)
                            {
                                ip_count += 1;
                            }
                            else
                            {
                                ip_count += 2;
                            }
                        }
                        else
                        {
                            ip_count += 2;
                        }
                    }
                    else if(in_thread && len == 3 && strncmp(p, "  }", 3) == 0)
                    {
                        int duplicate_id = 0;
                        for(size_t i = 0; i < parsed.thread_count; i += 1)
                        {
                            if(parsed.thread_ids[i] == thread_id)
                            {
                                duplicate_id = 1;
                            }
                        }
                        if(name_count != 1 || id_count != 1 || ip_count != 1 || duplicate_id)
                        {
                            parsed.thread_count = 0;
                            parsed.first_thread_id = 0;
                            break;
                        }
                        if(parsed.thread_count == 0)
                        {
                            parsed.first_thread_id = thread_id;
                        }
                        parsed.thread_ids[parsed.thread_count] = thread_id;
                        parsed.thread_ips[parsed.thread_count] = thread_ip;
                        parsed.thread_count += 1;
                        in_thread = 0;
                    }
                    p = end != NULL ? end + 1 : NULL;
                }
                if(in_thread)
                {
                    parsed.thread_count = 0;
                    parsed.first_thread_id = 0;
                }
            }
            else
            {
                parsed.thread_count = 0;
            }
            if(parsed.thread_count != 0 && parsed.first_thread_id != 0)
            {
                *state = parsed;
                ok = 1;
            }
        }
    }
    return ok;
}

static int
parse_eval(const char *text, EvalResult *result)
{
    int ok = 0;
    if(text != NULL && strlen(text) <= MAX_IPC_BYTES && strncmp(text, "eval:\n{", 7) == 0 && balanced_md(text))
    {
        EvalResult parsed = {0};
        if(read_field(text, 1, "expr", parsed.expr, sizeof(parsed.expr)) &&
           read_field(text, 1, "value", parsed.value, sizeof(parsed.value)) &&
           read_field(text, 1, "type", parsed.type, sizeof(parsed.type)) &&
           read_field(text, 1, "msgs", parsed.messages, sizeof(parsed.messages)))
        {
            *result = parsed;
            ok = 1;
        }
    }
    return ok;
}

static const char *path_basename(const char *path);

static int
line_matches(const char *text, const char *file_name, unsigned line_num)
{
    int found = 0;
    int in_lines = 0;
    int in_record = 0;
    char current_file[MAX_VALUE_BYTES] = {0};
    char current_line[MAX_VALUE_BYTES] = {0};
    const char *line = text;
    while(line != NULL && *line != 0)
    {
        const char *end = strchr(line, '\n');
        size_t len = end != NULL ? (size_t)(end - line) : strlen(line);
        if(len >= 7 && strncmp(line, " lines:", 7) == 0)
        {
            in_lines = 1;
        }
        else if(in_lines && len >= 9 && strncmp(line, " threads:", 9) == 0)
        {
            in_lines = 0;
        }
        else if(in_lines && len == 3 && strncmp(line, "  {", 3) == 0)
        {
            in_record = 1;
            current_file[0] = 0;
            current_line[0] = 0;
        }
        else if(in_lines && in_record && len >= 13 && strncmp(line, "   file_name:", 13) == 0)
        {
            char raw[MAX_VALUE_BYTES];
            size_t raw_len = len - 13;
            if(raw_len < sizeof(raw))
            {
                memcpy(raw, line + 13, raw_len);
                raw[raw_len] = 0;
                copy_decoded_value(raw, current_file, sizeof(current_file));
            }
        }
        else if(in_lines && in_record && len >= 12 && strncmp(line, "   line_num:", 12) == 0)
        {
            size_t value_len = len - 12;
            if(value_len < sizeof(current_line))
            {
                memcpy(current_line, line + 12, value_len);
                current_line[value_len] = 0;
            }
        }
        else if(in_lines && in_record && len == 3 && strncmp(line, "  }", 3) == 0)
        {
            uint64_t parsed_line = 0;
            if(strcmp(path_basename(current_file), path_basename(file_name)) == 0 &&
               parse_u64(current_line, &parsed_line) && parsed_line == line_num)
            {
                found = 1;
            }
            in_record = 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return found == 1;
}

static const char *
path_basename(const char *path)
{
    const char *result = path;
    for(const char *p = path; *p != 0; p += 1)
    {
        if(*p == '/')
        {
            result = p + 1;
        }
    }
    return result;
}

static int
scan_source_lines(const char *path, SourceLines *lines)
{
    int ok = 0;
    FILE *file = fopen(path, "rb");
    if(file != NULL)
    {
        SourceLines found = {0};
        unsigned outer_count = 0;
        unsigned inner_count = 0;
        unsigned inner_done_count = 0;
        unsigned worker_count = 0;
        char *text = NULL;
        size_t capacity = 0;
        ssize_t length = 0;
        unsigned line_num = 0;
        while((length = getline(&text, &capacity, file)) >= 0)
        {
            (void)length;
            line_num += 1;
            if(strstr(text, "RAD_BPT_OUTER") != NULL)
            {
                found.outer = line_num;
                outer_count += 1;
            }
            if(strstr(text, "RAD_BPT_INNER") != NULL)
            {
                found.inner = line_num;
                inner_count += 1;
            }
            if(strstr(text, "return inner_total;") != NULL)
            {
                found.inner_done = line_num;
                inner_done_count += 1;
            }
            if(strstr(text, "RAD_BPT_WORKER") != NULL)
            {
                found.worker = line_num;
                worker_count += 1;
            }
        }
        if(!ferror(file) && outer_count == 1 && inner_count == 1 && inner_done_count == 1 && worker_count == 1)
        {
            *lines = found;
            ok = 1;
        }
        free(text);
        fclose(file);
    }
    return ok;
}

static int
selected_thread_id(const State *state, uint64_t *thread_id_out)
{
    int matches = 0;
    uint64_t thread_id = 0;
    for(size_t i = 0; i < state->thread_count; i += 1)
    {
        if(state->thread_ips[i] == state->ip)
        {
            thread_id = state->thread_ids[i];
            matches += 1;
        }
    }
    if(matches == 1)
    {
        *thread_id_out = thread_id;
    }
    return matches == 1;
}

static int
state_has_thread_id(const State *state, uint64_t thread_id)
{
    int found = 0;
    for(size_t i = 0; i < state->thread_count; i += 1)
    {
        if(state->thread_ids[i] == thread_id)
        {
            found += 1;
        }
    }
    return found == 1;
}

static int
symbol_matches(const char *actual, const char *expected)
{
    int result = 0;
    size_t expected_len = strlen(expected);
    const char *suffix = strrchr(actual, ':');
    suffix = suffix != NULL ? suffix + 1 : actual;
    if(strcmp(suffix, expected) == 0 ||
       (strlen(suffix) > expected_len && strcmp(suffix + strlen(suffix) - expected_len, expected) == 0 &&
        suffix[strlen(suffix) - expected_len - 1] == ':'))
    {
        result = 1;
    }
    return result;
}

static int
drain_gui_output(Session *session)
{
    int ok = !session->failed;
    if(session->gui_output_fd >= 0)
    {
        int reading = 1;
        while(reading)
        {
            char chunk[8192];
            ssize_t got = read(session->gui_output_fd, chunk, sizeof(chunk));
            if(got > 0)
            {
                if(!buffer_append(&session->gui_output, chunk, (size_t)got))
                {
                    session->failed = 1;
                    ok = 0;
                    log_text("RADDBG_ORACLE_ERROR captured GUI/debuggee output exceeds 1 MiB cap", NULL);
                }
            }
            else if(got < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                reading = 0;
                if(got < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    session->failed = 1;
                    ok = 0;
                    log_text("RADDBG_ORACLE_ERROR failed reading GUI/debuggee output pipe", NULL);
                }
            }
        }
    }
    return ok;
}

static int
read_child_output(Session *session, pid_t child, int read_fd, uint64_t child_deadline, Buffer *output, int *status_out)
{
    int ok = 0;
    int failed = 0;
    int eof = 0;
    int child_done = 0;
    int status_valid = 0;
    int status = 0;
    while(!eof || !child_done)
    {
        if(!drain_gui_output(session))
        {
            failed = 1;
        }
        struct pollfd pfd = { .fd = read_fd, .events = POLLIN | POLLHUP };
        uint64_t now = monotonic_ms();
        int timeout = now < child_deadline ? (int)(child_deadline - now) : 0;
        if(timeout > 250)
        {
            timeout = 250;
        }
        int poll_result = poll(&pfd, 1, timeout);
        if(poll_result < 0 && errno != EINTR)
        {
            failed = 1;
            kill(-child, SIGKILL);
            eof = 1;
        }
        if(poll_result > 0 && (pfd.revents & (POLLIN | POLLHUP)) != 0)
        {
            char chunk[8192];
            ssize_t got = read(read_fd, chunk, sizeof(chunk));
            if(got > 0)
            {
                if(!buffer_append(output, chunk, (size_t)got))
                {
                    failed = 1;
                    log_text("RADDBG_ORACLE_ERROR output exceeds 1 MiB cap", NULL);
                    kill(-child, SIGKILL);
                    eof = 1;
                }
            }
            else if(got == 0)
            {
                eof = 1;
            }
            else if(errno != EINTR && errno != EAGAIN)
            {
                failed = 1;
                eof = 1;
            }
        }
        if(!child_done)
        {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if(waited == child)
            {
                child_done = 1;
                status_valid = 1;
            }
            else if(waited < 0 && errno != EINTR)
            {
                failed = 1;
                child_done = 1;
            }
        }
        if(monotonic_ms() >= child_deadline && (!eof || !child_done))
        {
            failed = 1;
            kill(-child, SIGKILL);
            pid_t waited = waitpid(child, &status, 0);
            if(waited == child)
            {
                status_valid = 1;
            }
            else
            {
                failed = 1;
            }
            child_done = 1;
            eof = 1;
            log_text("RADDBG_ORACLE_ERROR IPC helper timed out", NULL);
        }
        if(g_interrupted)
        {
            failed = 1;
            kill(-child, SIGKILL);
            pid_t waited = waitpid(child, &status, 0);
            if(waited == child)
            {
                status_valid = 1;
            }
            child_done = 1;
            eof = 1;
        }
    }
    close(read_fd);
    if(output->data == NULL)
    {
        output->data = calloc(1, 1);
    }
    if(output->data != NULL && child_done && status_valid && !failed && WIFEXITED(status) &&
       WEXITSTATUS(status) == 0 && !session->failed && !g_interrupted)
    {
        *status_out = status;
        ok = 1;
    }
    return ok;
}

static int
tcp_listener_inode(uint16_t port, unsigned long *inode_out)
{
    int found = 0;
    int readable = 0;
    const char *paths[] = {"/proc/net/tcp", "/proc/net/tcp6"};
    for(size_t path_idx = 0; path_idx < sizeof(paths) / sizeof(paths[0]); path_idx += 1)
    {
        FILE *file = fopen(paths[path_idx], "r");
        if(file != NULL)
        {
            readable += 1;
            char *line = NULL;
            size_t capacity = 0;
            ssize_t length = 0;
            while(!found && (length = getline(&line, &capacity, file)) >= 0)
            {
                (void)length;
                unsigned slot = 0;
                unsigned uid = 0;
                unsigned long timeout = 0;
                unsigned long inode = 0;
                char local[80] = {0};
                char remote[80] = {0};
                char state[8] = {0};
                char queue[80] = {0};
                char timers[80] = {0};
                char retransmits[80] = {0};
                int fields = sscanf(line, " %u: %79s %79s %7s %79s %79s %u %lu %lu",
                                    &slot, local, remote, state, queue, timers, &uid, &timeout, &inode);
                char *colon = strchr(local, ':');
                if(fields == 9 && colon != NULL)
                {
                    errno = 0;
                    char *end = NULL;
                    unsigned long parsed_port = strtoul(colon + 1, &end, 16);
                    if(errno == 0 && end != colon + 1 && *end == 0 && strcmp(state, "0A") == 0 &&
                       parsed_port == port)
                    {
                        *inode_out = inode;
                        found = 1;
                    }
                }
            }
            free(line);
            if(ferror(file))
            {
                readable = -100;
            }
            fclose(file);
        }
    }
    return readable > 0 ? found : -1;
}

static int
process_owns_socket_inode(pid_t pid, unsigned long inode)
{
    int found = 0;
    char fd_dir_path[64];
    snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%ld/fd", (long)pid);
    DIR *fd_dir = opendir(fd_dir_path);
    if(fd_dir != NULL)
    {
        struct dirent *entry = NULL;
        char expected[96];
        snprintf(expected, sizeof(expected), "socket:[%lu]", inode);
        while(!found && (entry = readdir(fd_dir)) != NULL)
        {
            if(entry->d_name[0] != '.')
            {
                char link_path[PATH_MAX];
                char target[128];
                int written = snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir_path, entry->d_name);
                if(written > 0 && (size_t)written < sizeof(link_path))
                {
                    ssize_t length = readlink(link_path, target, sizeof(target) - 1);
                    if(length >= 0)
                    {
                        target[length] = 0;
                        if(strcmp(target, expected) == 0)
                        {
                            found = 1;
                        }
                    }
                }
            }
        }
        closedir(fd_dir);
    }
    return found;
}

static int
process_owns_ipc_listener(Session *session)
{
    int owns = 0;
    unsigned long inode = 0;
    if(session->gui_pid > 0 && tcp_listener_inode(session->args.port, &inode) == 1)
    {
        owns = process_owns_socket_inode(session->gui_pid, inode);
    }
    return owns;
}

static int
wait_for_owned_ipc(Session *session)
{
    int ok = 0;
    while(monotonic_ms() < session->deadline_ms && !g_interrupted)
    {
        unsigned long inode = 0;
        int listener = tcp_listener_inode(session->args.port, &inode);
        if(listener < 0)
        {
            log_text("RADDBG_ORACLE_ERROR cannot inspect Linux TCP listener ownership", NULL);
            break;
        }
        if(listener == 1)
        {
            if(process_owns_socket_inode(session->gui_pid, inode))
            {
                ok = 1;
            }
            else
            {
                log_text("RADDBG_ORACLE_ERROR requested IPC port is owned by another process", NULL);
            }
            break;
        }
        int status = 0;
        if(session->gui_pid <= 0 || waitpid(session->gui_pid, &status, WNOHANG) == session->gui_pid)
        {
            session->gui_pid = 0;
            fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_ERROR GUI exited before owning IPC port, status=%d\n", status);
            break;
        }
        drain_gui_output(session);
        usleep(POLL_INTERVAL_MS * 1000);
    }
    if(!ok)
    {
        log_text("RADDBG_ORACLE_ERROR child-owned IPC listener not observed before deadline", NULL);
    }
    return ok;
}

static int
run_ipc(Session *session, const char *command, Buffer *output)
{
    int ok = 0;
    int pipes[2] = {-1, -1};
    pid_t child = -1;
    if(session->gui_pid > 0 && process_owns_ipc_listener(session) && pipe(pipes) == 0)
    {
        child = fork();
        if(child == 0)
        {
            setpgid(0, 0);
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);
            close(pipes[1]);
            char *const child_argv[] = {
                (char *)session->args.raddbg,
                (char *)"--ipc",
                session->port_arg,
                (char *)command,
                NULL,
            };
            execv(session->args.raddbg, child_argv);
            _exit(127);
        }
        else if(child > 0)
        {
            close(pipes[1]);
            setpgid(child, child);
            int status = 0;
            uint64_t child_deadline = monotonic_ms() + 10000u;
            if(child_deadline > session->deadline_ms)
            {
                child_deadline = session->deadline_ms;
            }
            ok = read_child_output(session, child, pipes[0], child_deadline, output, &status);
        }
        else
        {
            close(pipes[0]);
            close(pipes[1]);
        }
    }
    else if(session->gui_pid > 0)
    {
        log_text("RADDBG_ORACLE_ERROR refusing IPC command because child does not own requested listener", NULL);
    }
    if(!ok && child > 0)
    {
        kill(-child, SIGKILL);
    }
    return ok;
}

static int
command(Session *session, const char *text, Buffer *response)
{
    int ok = run_ipc(session, text, response);
    fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_COMMAND %s\n", text);
    fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_RESPONSE_BEGIN\n%s\nRADDBG_ORACLE_RESPONSE_END\n",
            response->data != NULL ? response->data : "<no response>");
    fflush(g_log != NULL ? g_log : stdout);
    return ok && response->size != 0;
}

static int
state_query_raw(Session *session, State *state, Buffer *response_out)
{
    int ok = 0;
    Buffer response = {0};
    if(command(session, "state", &response))
    {
        ok = parse_state(response.data, state);
        if(!ok)
        {
            fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_ERROR malformed or ambiguous state response\n");
        }
    }
    if(response_out != NULL)
    {
        *response_out = response;
    }
    else
    {
        free(response.data);
    }
    return ok;
}

static int
state_query(Session *session, State *state)
{
    return state_query_raw(session, state, NULL);
}

static int
wait_for_stop(Session *session, uint64_t old_stop_count, uint64_t old_run_gen, const char *expected_symbol,
              unsigned expected_line, State *state_out)
{
    int ok = 0;
    while(monotonic_ms() < session->deadline_ms && !g_interrupted)
    {
        State state = {0};
        Buffer response = {0};
        if(state_query_raw(session, &state, &response))
        {
            if(!state.running && state.stop_count > old_stop_count && state.run_gen > old_run_gen)
            {
                uint64_t selected_id = 0;
                int location_ok = selected_thread_id(&state, &selected_id) &&
                                  symbol_matches(state.symbol, expected_symbol) &&
                                  (expected_line == 0 || line_matches(response.data, session->args.source, expected_line));
                if(location_ok)
                {
                    if(session->target_pid == 0)
                    {
                        if(state.first_thread_id == 0 || state.first_thread_id > INT_MAX)
                        {
                            log_text("RADDBG_ORACLE_ERROR could not identify Linux debuggee PID from first stopped thread", NULL);
                        }
                        else
                        {
                            session->target_pid = (pid_t)state.first_thread_id;
                        }
                    }
                    if(session->target_pid != 0)
                    {
                        *state_out = state;
                        ok = 1;
                    }
                }
                else
                {
                    fprintf(g_log != NULL ? g_log : stdout,
                            "RADDBG_ORACLE_ERROR stopped at unexpected symbol/line '%s' (wanted '%s' line %u)\n",
                            state.symbol, expected_symbol, expected_line);
                    free(response.data);
                    break;
                }
            }
        }
        free(response.data);
        if(ok)
        {
            break;
        }
        drain_gui_output(session);
        usleep(POLL_INTERVAL_MS * 1000);
        int status = 0;
        if(session->gui_pid > 0 && waitpid(session->gui_pid, &status, WNOHANG) == session->gui_pid)
        {
            session->gui_pid = 0;
            fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_ERROR GUI exited while awaiting stop, status=%d\n", status);
            break;
        }
    }
    return ok;
}

static int
send_and_wait(Session *session, const char *command_text, const State *before,
              const char *symbol, unsigned line, State *after)
{
    int ok = 0;
    Buffer response = {0};
    if(command(session, command_text, &response))
    {
        ok = wait_for_stop(session, before->stop_count, before->run_gen, symbol, line, after);
    }
    free(response.data);
    return ok;
}

static int
has_local(const char *state_text, const char *name)
{
    int found = 0;
    const char *block = strstr(state_text, " locals:\n {\n");
    if(block != NULL)
    {
        const char *end = strstr(block, "\n }\n");
        const char *line = strchr(block, '\n');
        if(line != NULL)
        {
            line += 1;
        }
        while(line != NULL && end != NULL && line < end)
        {
            const char *line_end = strchr(line, '\n');
            if(line_end == NULL || line_end > end)
            {
                line_end = end;
            }
            if(line < line_end && line[0] == ' ' && line[1] != ' ')
            {
                size_t len = (size_t)(line_end - (line + 1));
                if(len == strlen(name) && strncmp(line + 1, name, len) == 0)
                {
                    found += 1;
                }
            }
            line = line_end < end ? line_end : NULL;
        }
    }
    return found == 1;
}

static int
eval_matches(const EvalResult *eval, const char *expr, const char *expected)
{
    return strcmp(eval->expr, expr) == 0 && strcmp(eval->value, expected) == 0 &&
           strstr(eval->type, "int") != NULL && eval->messages[0] == 0;
}

static int
eval_once(Session *session, const char *expr, const char *expected, uint64_t stop_count)
{
    int ok = 0;
    char command_text[MAX_VALUE_BYTES * 2];
    snprintf(command_text, sizeof(command_text), "eval %s", expr);
    for(unsigned attempt = 0; attempt < 3 && !ok; attempt += 1)
    {
        Buffer response = {0};
        EvalResult eval = {0};
        State before = {0};
        State after = {0};
        int pre_stable = state_query(session, &before) && !before.running && before.stop_count == stop_count;
        int eval_parsed = pre_stable && command(session, command_text, &response) && parse_eval(response.data, &eval);
        int post_stable = eval_parsed && state_query(session, &after) && !after.running &&
                          after.stop_count == stop_count && before.run_gen == after.run_gen;
        if(post_stable && eval_matches(&eval, expr, expected))
        {
            ok = 1;
        }
        else if(eval_parsed)
        {
            fprintf(g_log != NULL ? g_log : stdout,
                    "RADDBG_ORACLE_EVAL_MISMATCH expr=%s expected=%s actual=%s type=%s attempt=%u\n",
                    expr, expected, eval.value, eval.type, attempt + 1);
        }
        free(response.data);
        if(!ok)
        {
            usleep(50 * 1000);
        }
    }
    return ok;
}

static int
expect_values(Session *session, const State *state, const ExpectedValue *values, size_t count)
{
    int ok = 1;
    Buffer response = {0};
    State current = {0};
    if(!state_query_raw(session, &current, &response) || current.stop_count != state->stop_count || current.running)
    {
        ok = 0;
    }
    for(size_t i = 0; i < count; i += 1)
    {
        char base[MAX_VALUE_BYTES];
        size_t out = 0;
        while(values[i].expr[out] != 0 && values[i].expr[out] != '[' && values[i].expr[out] != '.' && values[i].expr[out] != '-' && out + 1 < sizeof(base))
        {
            base[out] = values[i].expr[out];
            out += 1;
        }
        base[out] = 0;
        if(base[0] == 0 || !has_local(response.data != NULL ? response.data : "", base) ||
           !eval_once(session, values[i].expr, values[i].value, state->stop_count))
        {
            ok = 0;
        }
    }
    free(response.data);
    return ok;
}

static int
window_class_visible(Display *display, Window root)
{
    int found = 0;
    Window queue[MAX_X11_NODES];
    unsigned depths[MAX_X11_NODES];
    unsigned head = 0;
    unsigned tail = 0;
    queue[tail] = root;
    depths[tail] = 0;
    tail += 1;
    while(head < tail && !found)
    {
        Window parent = queue[head];
        unsigned depth = depths[head];
        head += 1;
        if(depth <= MAX_X11_DEPTH)
        {
            Window root_return = 0;
            Window parent_return = 0;
            Window *children = NULL;
            unsigned child_count = 0;
            if(XQueryTree(display, parent, &root_return, &parent_return, &children, &child_count) != 0)
            {
                for(unsigned i = 0; i < child_count && tail < MAX_X11_NODES && !found; i += 1)
                {
                    Window child = children[i];
                    XClassHint hint = {0};
                    XWindowAttributes attributes = {0};
                    if(XGetClassHint(display, child, &hint) != 0)
                    {
                        if(hint.res_class != NULL && strcmp(hint.res_class, "RADDBG") == 0 &&
                           XGetWindowAttributes(display, child, &attributes) != 0 &&
                           attributes.map_state == IsViewable)
                        {
                            found = 1;
                        }
                        if(hint.res_name != NULL) XFree(hint.res_name);
                        if(hint.res_class != NULL) XFree(hint.res_class);
                    }
                    if(!found && depth < MAX_X11_DEPTH)
                    {
                        queue[tail] = child;
                        depths[tail] = depth + 1;
                        tail += 1;
                    }
                }
                if(children != NULL)
                {
                    XFree(children);
                }
            }
        }
    }
    return found;
}

static int
wait_for_gui_window(Session *session)
{
    int ok = 0;
    if(session->display != NULL)
    {
        while(monotonic_ms() < session->deadline_ms && !g_interrupted)
        {
            if(window_class_visible(session->display, DefaultRootWindow(session->display)))
            {
                ok = 1;
                break;
            }
            int status = 0;
            if(session->gui_pid > 0 && waitpid(session->gui_pid, &status, WNOHANG) == session->gui_pid)
            {
                session->gui_pid = 0;
                fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_ERROR GUI exited before mapping window, status=%d\n", status);
                break;
            }
            drain_gui_output(session);
            usleep(POLL_INTERVAL_MS * 1000);
        }
    }
    if(!ok)
    {
        log_text("RADDBG_ORACLE_ERROR mapped RADDBG X11 window not observed before deadline", NULL);
    }
    return ok;
}

static int
format_path(char *dst, size_t dst_cap, const char *format, const char *path)
{
    int written = snprintf(dst, dst_cap, format, path);
    return written >= 0 && (size_t)written < dst_cap;
}

static int
prepare_session(Session *session)
{
    int ok = 0;
    char user_arg[PATH_MAX + 32];
    char project_arg[PATH_MAX + 32];
    char logs_arg[PATH_MAX + 32];
    int output_pipes[2] = {-1, -1};
    if(session->display == NULL)
    {
        session->display = XOpenDisplay(NULL);
    }
    if(session->display == NULL)
    {
        log_text("RADDBG_ORACLE_ERROR cannot open X11 display", NULL);
    }
    else if(window_class_visible(session->display, DefaultRootWindow(session->display)))
    {
        log_text("RADDBG_ORACLE_ERROR preexisting RADDBG window found; use an isolated X server", NULL);
    }
    else
    {
        unsigned long occupied_inode = 0;
        int listener = tcp_listener_inode(session->args.port, &occupied_inode);
        if(listener < 0)
        {
            log_text("RADDBG_ORACLE_ERROR cannot inspect Linux TCP listeners", NULL);
        }
        else if(listener == 1)
        {
            log_text("RADDBG_ORACLE_ERROR requested IPC port is already in use", NULL);
        }
        else if(mkdir(session->args.session_dir, 0700) == 0)
        {
            int paths_ok = format_path(session->temp_dir, sizeof(session->temp_dir), "%s", session->args.session_dir) &&
                           format_path(session->user_path, sizeof(session->user_path), "%s/session.raddbg_user", session->temp_dir) &&
                           format_path(session->project_path, sizeof(session->project_path), "%s/session.raddbg_project", session->temp_dir) &&
                           format_path(session->logs_path, sizeof(session->logs_path), "%s/logs", session->temp_dir);
            snprintf(session->port_arg, sizeof(session->port_arg), "--ipc_port:%u", session->args.port);
            paths_ok = paths_ok && format_path(user_arg, sizeof(user_arg), "--user:%s", session->user_path) &&
                       format_path(project_arg, sizeof(project_arg), "--project:%s", session->project_path) &&
                       format_path(logs_arg, sizeof(logs_arg), "--logs:%s", session->logs_path);
            if(paths_ok && mkdir(session->logs_path, 0700) == 0 && pipe(output_pipes) == 0)
            {
                fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_SESSION_DIR %s\n", session->temp_dir);
                ok = 1;
            }
        }
    }
    if(ok)
    {
        pid_t child = fork();
        if(child == 0)
        {
            setpgid(0, 0);
            close(output_pipes[0]);
            if(dup2(output_pipes[1], STDOUT_FILENO) < 0 || dup2(output_pipes[1], STDERR_FILENO) < 0)
            {
                _exit(126);
            }
            close(output_pipes[1]);
            char *const child_argv[] = {
                (char *)session->args.raddbg,
                user_arg,
                project_arg,
                logs_arg,
                session->port_arg,
                (char *)session->args.debuggee,
                NULL,
            };
            execv(session->args.raddbg, child_argv);
            _exit(127);
        }
        else if(child > 0)
        {
            close(output_pipes[1]);
            session->gui_output_fd = output_pipes[0];
            int fd_flags = fcntl(session->gui_output_fd, F_GETFL, 0);
            int descriptor_flags = fcntl(session->gui_output_fd, F_GETFD, 0);
            if(fd_flags >= 0 && descriptor_flags >= 0 &&
               fcntl(session->gui_output_fd, F_SETFL, fd_flags | O_NONBLOCK) == 0)
            {
                fcntl(session->gui_output_fd, F_SETFD, descriptor_flags | FD_CLOEXEC);
                session->gui_pid = child;
                session->gui_group = child;
                setpgid(child, child);
                ok = 1;
            }
            else
            {
                close(session->gui_output_fd);
                session->gui_output_fd = -1;
                session->gui_pid = child;
                session->gui_group = child;
                setpgid(child, child);
                ok = 0;
            }
        }
        else
        {
            close(output_pipes[0]);
            close(output_pipes[1]);
            ok = 0;
        }
    }
    if(ok && wait_for_gui_window(session) && wait_for_owned_ipc(session))
    {
        ok = 1;
    }
    else
    {
        ok = 0;
    }
    return ok;
}

static int
response_equals(const Buffer *response, const char *expected)
{
    int matches = 0;
    if(response->data != NULL)
    {
        const char *begin = response->data;
        size_t length = response->size;
        while(length != 0 && (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n'))
        {
            begin += 1;
            length -= 1;
        }
        while(length != 0 && (begin[length - 1] == ' ' || begin[length - 1] == '\t' ||
                              begin[length - 1] == '\r' || begin[length - 1] == '\n'))
        {
            length -= 1;
        }
        matches = length == strlen(expected) && strncmp(begin, expected, length) == 0;
    }
    return matches;
}

static int
gui_output_line_count(const Session *session, const char *expected)
{
    int count = 0;
    if(session->gui_output.data != NULL)
    {
        const char *line = session->gui_output.data;
        size_t expected_len = strlen(expected);
        while(line != NULL && *line != 0)
        {
            const char *end = strchr(line, '\n');
            size_t len = end != NULL ? (size_t)(end - line) : strlen(line);
            if(len != 0 && line[len - 1] == '\r')
            {
                len -= 1;
            }
            if(len == expected_len && strncmp(line, expected, len) == 0)
            {
                count += 1;
            }
            line = end != NULL ? end + 1 : NULL;
        }
    }
    return count;
}

static int
target_pid_alive(pid_t pid)
{
    int alive = 1;
    if(pid <= 0)
    {
        alive = 0;
    }
    else if(kill(pid, 0) != 0 && errno == ESRCH)
    {
        alive = 0;
    }
    return alive;
}

static int
wait_for_target_completion(Session *session)
{
    static const char expected[] =
        "RADDEBUGGER_DEBUGGEE main=329 worker=337 values=2,3,5 record=17,5,257,7,11,13 thread=joined";
    int ok = 0;
    Buffer response = {0};
    if(session->target_pid > 0 && command(session, "continue", &response) && response.size != 0)
    {
        uint64_t marker_deadline = 0;
        while(monotonic_ms() < session->deadline_ms && !g_interrupted && !session->failed)
        {
            drain_gui_output(session);
            int marker_count = gui_output_line_count(session, expected);
            if(marker_count > 1)
            {
                log_text("RADDBG_ORACLE_ERROR debuggee completion marker appeared more than once", NULL);
                break;
            }
            int alive = target_pid_alive(session->target_pid);
            if(marker_count == 1 && !alive)
            {
                ok = 1;
                break;
            }
            if(!alive && marker_deadline == 0)
            {
                marker_deadline = monotonic_ms() + 250u;
            }
            if(marker_deadline != 0 && monotonic_ms() >= marker_deadline)
            {
                break;
            }
            usleep(POLL_INTERVAL_MS * 1000);
        }
    }
    free(response.data);
    drain_gui_output(session);
    if(ok && gui_output_line_count(session, expected) == 1)
    {
        FILE *stream = g_log != NULL ? g_log : stdout;
        fputs("RADDBG_GUI_OUTPUT_BEGIN\n", stream);
        if(session->gui_output.data != NULL)
        {
            fwrite(session->gui_output.data, 1, session->gui_output.size, stream);
            if(session->gui_output.size != 0 && session->gui_output.data[session->gui_output.size - 1] != '\n')
            {
                fputc('\n', stream);
            }
        }
        fputs("RADDBG_GUI_OUTPUT_END\n", stream);
        fflush(stream);
    }
    else
    {
        log_text("RADDBG_ORACLE_ERROR target did not exit after emitting the exact fixture marker", NULL);
        ok = 0;
    }
    return ok;
}

static int
cleanup_session(Session *session)
{
    int ok = 1;
    session->deadline_ms = monotonic_ms() + 10000u;
    if(session->gui_pid > 0)
    {
        Buffer response = {0};
        if(process_owns_ipc_listener(session) && run_ipc(session, "kill_all", &response) && response_equals(&response, "done"))
        {
            log_text("RADDBG_ORACLE_CLEANUP kill_all=done", NULL);
        }
        else
        {
            log_text("RADDBG_ORACLE_CLEANUP kill_all=unavailable; terminating the owned tracer", NULL);
            ok = 0;
        }
        free(response.data);
        kill(-session->gui_group, SIGTERM);
        uint64_t end = monotonic_ms() + 1000u;
        int status = 0;
        int reaped = 0;
        while(monotonic_ms() < end && !reaped)
        {
            pid_t waited = waitpid(session->gui_pid, &status, WNOHANG);
            if(waited == session->gui_pid)
            {
                reaped = 1;
            }
            else if(waited < 0 && errno != EINTR)
            {
                ok = 0;
                break;
            }
            usleep(20 * 1000);
        }
        if(!reaped)
        {
            kill(-session->gui_group, SIGKILL);
            pid_t waited = -1;
            do
            {
                waited = waitpid(session->gui_pid, &status, 0);
            }
            while(waited < 0 && errno == EINTR);
            if(waited == session->gui_pid)
            {
                reaped = 1;
            }
            else
            {
                ok = 0;
            }
        }
        kill(-session->gui_group, SIGKILL);
        session->gui_pid = 0;
        if(!reaped)
        {
            log_text("RADDBG_ORACLE_ERROR could not reap RAD GUI process", NULL);
        }
    }
    if(session->target_pid > 0)
    {
        uint64_t end = monotonic_ms() + 2000u;
        while(target_pid_alive(session->target_pid) && monotonic_ms() < end)
        {
            usleep(20 * 1000);
        }
        if(target_pid_alive(session->target_pid))
        {
            log_text("RADDBG_ORACLE_ERROR debuggee process still exists after tracer cleanup", NULL);
            ok = 0;
        }
    }
    if(!drain_gui_output(session))
    {
        ok = 0;
    }
    if(session->gui_output_fd >= 0)
    {
        close(session->gui_output_fd);
        session->gui_output_fd = -1;
    }
    if(session->display != NULL)
    {
        XCloseDisplay(session->display);
        session->display = NULL;
    }
    free(session->gui_output.data);
    session->gui_output.data = NULL;
    session->gui_output.size = 0;
    if(ok)
    {
        log_text("RADDBG_ORACLE_CLEANUP status=pass", NULL);
    }
    return ok;
}

static int
parse_args(int argc, char **argv, Args *args)
{
    int ok = 1;
    memset(args, 0, sizeof(*args));
    args->timeout_ms = DEFAULT_TIMEOUT_MS;
    for(int i = 1; i < argc && ok; i += 1)
    {
        const char *key = argv[i];
        if(strcmp(key, "--self-test") == 0)
        {
            args->self_test = 1;
        }
        else if(i + 1 < argc)
        {
            const char *value = argv[++i];
            if(strcmp(key, "--raddbg") == 0) args->raddbg = value;
            else if(strcmp(key, "--debuggee") == 0) args->debuggee = value;
            else if(strcmp(key, "--source") == 0) args->source = value;
            else if(strcmp(key, "--session-dir") == 0) args->session_dir = value;
            else if(strcmp(key, "--port") == 0)
            {
                uint64_t parsed = 0;
                if(parse_u64(value, &parsed) && parsed > 0 && parsed < 65536)
                {
                    args->port = (uint16_t)parsed;
                }
                else
                {
                    ok = 0;
                }
            }
            else if(strcmp(key, "--timeout-ms") == 0)
            {
                uint64_t parsed = 0;
                if(parse_u64(value, &parsed) && parsed >= 1000 && parsed <= 600000)
                {
                    args->timeout_ms = (uint32_t)parsed;
                }
                else
                {
                    ok = 0;
                }
            }
            else
            {
                ok = 0;
            }
        }
        else
        {
            ok = 0;
        }
    }
    if(!args->self_test && (args->raddbg == NULL || args->debuggee == NULL || args->source == NULL ||
                           args->session_dir == NULL || args->port == 0))
    {
        ok = 0;
    }
    return ok;
}

static int
self_test(void)
{
    int ok = 1;
    const char *valid_state =
        "state:\n{\n running: 0\n run_gen: 3\n stop_count: 4\n ip: 0x10\n"
        " ip_module: \"fixture\"\n ip_voff: 0x2\n ip_voff_symbol: \"debuggee_inner\"\n"
        " locals:\n {\n  seed\n }\n lines:\n {\n  {\n   file_name: \"fixture.c\"\n   line_num: 31\n  }\n }\n"
        " threads:\n {\n  {\n   name: \"main\"\n   id: 1\n   ip: 0x10\n  }\n }\n modules:\n {\n }\n}\n";
    const char *duplicate_state =
        "state:\n{\n running: 0\n run_gen: 3\n stop_count: 4\n stop_count: 5\n ip: 0x10\n"
        " ip_module: \"fixture\"\n ip_voff: 0x2\n ip_voff_symbol: \"debuggee_inner\"\n"
        " threads:\n {\n  {\n   id: 1\n   ip: 0x10\n  }\n }\n modules:\n {\n }\n}\n";
    const char *missing_state =
        "state:\n{\n running: 0\n run_gen: 3\n stop_count: 4\n ip: 0x10\n"
        " ip_module: \"fixture\"\n ip_voff: 0x2\n"
        " threads:\n {\n  {\n   id: 1\n   ip: 0x10\n  }\n }\n modules:\n {\n }\n}\n";
    const char *truncated_state = "state:\n{\n running: 0\n stop_count: 4\n";
    const char *valid_eval = "eval:\n{\n expr: inner_value\n value: \"17\"\n type: \"int\"\n msgs: \"\"\n}\n";
    const char *duplicate_eval = "eval:\n{\n expr: inner_value\n expr: wrong\n value: \"17\"\n type: \"int\"\n msgs: \"\"\n}\n";
    const char *truncated_eval = "eval:\n{\n expr: inner_value\n value: \"17\"\n";
    State state = {0};
    EvalResult eval = {0};
    EvalResult bad_eval = {0};
    Buffer cap = {0};
    char *big = malloc(MAX_IPC_BYTES + 1u);
    if(!parse_state(valid_state, &state) || state.stop_count != 4 || state.running ||
       state.thread_count != 1 || state.first_thread_id != 1 || !line_matches(valid_state, "fixture.c", 31) ||
       !selected_thread_id(&state, &state.first_thread_id) ||
       parse_state(duplicate_state, &state) || parse_state(missing_state, &state) || parse_state(truncated_state, &state) ||
       !parse_eval(valid_eval, &eval) || !eval_matches(&eval, "inner_value", "17") ||
       line_matches(valid_state, "fixture.c", 32) || parse_eval(duplicate_eval, &bad_eval) || parse_eval(truncated_eval, &bad_eval))
    {
        ok = 0;
    }
    if(eval_matches(&eval, "inner_value", "18") || eval_matches(&eval, "wrong_name", "17"))
    {
        ok = 0;
    }
    bad_eval = eval;
    snprintf(bad_eval.messages, sizeof(bad_eval.messages), "evaluation error");
    if(eval_matches(&bad_eval, "inner_value", "17"))
    {
        ok = 0;
    }
    bad_eval = eval;
    snprintf(bad_eval.type, sizeof(bad_eval.type), "float");
    if(eval_matches(&bad_eval, "inner_value", "17"))
    {
        ok = 0;
    }
    if(big != NULL)
    {
        memset(big, 'x', MAX_IPC_BYTES + 1u);
        if(!buffer_append(&cap, big, MAX_IPC_BYTES) || buffer_append(&cap, "x", 1))
        {
            ok = 0;
        }
        free(big);
    }
    else
    {
        ok = 0;
    }
    free(cap.data);
    if(ok)
    {
        log_text("RADDBG_ORACLE_SELF_TEST status=pass", NULL);
    }
    else
    {
        log_text("RADDBG_ORACLE_SELF_TEST status=fail", NULL);
    }
    return ok;
}

static int
session_test(const Args *args)
{
    int ok = 0;
    Session session = {0};
    SourceLines lines = {0};
    session.args = *args;
    session.gui_output_fd = -1;
    session.deadline_ms = monotonic_ms() + args->timeout_ms;
    int source_ok = strchr(args->source, ' ') == NULL && scan_source_lines(args->source, &lines);
    if(!source_ok)
    {
        log_text("RADDBG_ORACLE_ERROR source path has spaces or fixture breakpoint markers are missing/ambiguous", NULL);
    }
    if(source_ok && prepare_session(&session))
    {
        State state = {0};
        State next = {0};
        State initial = {0};
        ExpectedValue outer_values[] = {
            {"outer_value", "6"}, {"values[0]", "2"}, {"values[1]", "3"}, {"values[2]", "5"},
            {"record.tag", "17"}, {"record.flags", "5"}, {"record.code", "257"},
            {"record.samples[0]", "7"}, {"record.samples[1]", "11"}, {"record.samples[2]", "13"},
        };
        ExpectedValue inner_values[] = {
            {"seed", "6"}, {"inner_value", "17"},
            {"record.tag", "17"}, {"record.flags", "5"},
            {"record.code", "257"}, {"record.samples[0]", "7"}, {"record.samples[1]", "11"}, {"record.samples[2]", "13"},
        };
        ExpectedValue main_inner_total[] = {{"inner_total", "316"}};
        ExpectedValue worker_values[] = {{"worker_seed", "7"}};
        ExpectedValue worker_outer_values[] = {
            {"outer_value", "10"}, {"values[0]", "2"}, {"values[1]", "3"}, {"values[2]", "5"},
            {"record.tag", "17"}, {"record.flags", "5"}, {"record.code", "257"},
            {"record.samples[0]", "7"}, {"record.samples[1]", "11"}, {"record.samples[2]", "13"},
        };
        ExpectedValue worker_inner_values[] = {
            {"seed", "10"}, {"inner_value", "21"},
            {"record.tag", "17"}, {"record.flags", "5"}, {"record.code", "257"},
            {"record.samples[0]", "7"}, {"record.samples[1]", "11"}, {"record.samples[2]", "13"},
        };
        ExpectedValue worker_inner_total[] = {{"inner_total", "320"}};
        char command_text[PATH_MAX + 128];
        int sequence_ok = 1;

        snprintf(command_text, sizeof(command_text), "run_to_name debuggee_outer");
        if(!send_and_wait(&session, command_text, &initial, "debuggee_outer", 0, &next)) sequence_ok = 0;
        if(sequence_ok) state = next;
        if(sequence_ok && (state.thread_count != 1 || state.first_thread_id == 0)) sequence_ok = 0;

        snprintf(command_text, sizeof(command_text), "run_to_line %s:%u", args->source, lines.outer);
        if(sequence_ok && !send_and_wait(&session, command_text, &state, "debuggee_outer", lines.outer, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            if(state.thread_count != 1 || !expect_values(&session, &state, outer_values, sizeof(outer_values) / sizeof(outer_values[0]))) sequence_ok = 0;
        }

        if(sequence_ok && !send_and_wait(&session, "step_into", &state, "debuggee_inner", 0, &next)) sequence_ok = 0;
        if(sequence_ok) state = next;
        if(sequence_ok && !send_and_wait(&session, "step_over", &state, "debuggee_inner", lines.inner, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            if(!expect_values(&session, &state, inner_values, sizeof(inner_values) / sizeof(inner_values[0]))) sequence_ok = 0;
        }
        snprintf(command_text, sizeof(command_text), "run_to_line %s:%u", args->source, lines.inner_done);
        if(sequence_ok && !send_and_wait(&session, command_text, &state, "debuggee_inner", lines.inner_done, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            if(!expect_values(&session, &state, main_inner_total, sizeof(main_inner_total) / sizeof(main_inner_total[0]))) sequence_ok = 0;
        }

        if(sequence_ok && !send_and_wait(&session, "step_out", &state, "debuggee_outer", 0, &next)) sequence_ok = 0;
        if(sequence_ok) state = next;

        snprintf(command_text, sizeof(command_text), "run_to_line %s:%u", args->source, lines.worker);
        if(sequence_ok && !send_and_wait(&session, command_text, &state, "debuggee_worker", lines.worker, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            uint64_t worker_id = 0;
            if(state.thread_count != 2 || !selected_thread_id(&state, &worker_id) || worker_id == session.target_pid ||
               !state_has_thread_id(&state, (uint64_t)session.target_pid) ||
               !expect_values(&session, &state, worker_values, sizeof(worker_values) / sizeof(worker_values[0]))) sequence_ok = 0;
        }

        snprintf(command_text, sizeof(command_text), "run_to_line %s:%u", args->source, lines.outer);
        if(sequence_ok && !send_and_wait(&session, command_text, &state, "debuggee_outer", lines.outer, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            if(!expect_values(&session, &state, worker_outer_values, sizeof(worker_outer_values) / sizeof(worker_outer_values[0]))) sequence_ok = 0;
        }

        if(sequence_ok && !send_and_wait(&session, "step_into", &state, "debuggee_inner", 0, &next)) sequence_ok = 0;
        if(sequence_ok) state = next;
        if(sequence_ok && !send_and_wait(&session, "step_over", &state, "debuggee_inner", lines.inner, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            if(!expect_values(&session, &state, worker_inner_values, sizeof(worker_inner_values) / sizeof(worker_inner_values[0]))) sequence_ok = 0;
        }
        snprintf(command_text, sizeof(command_text), "run_to_line %s:%u", args->source, lines.inner_done);
        if(sequence_ok && !send_and_wait(&session, command_text, &state, "debuggee_inner", lines.inner_done, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            if(!expect_values(&session, &state, worker_inner_total, sizeof(worker_inner_total) / sizeof(worker_inner_total[0]))) sequence_ok = 0;
        }
        if(sequence_ok)
        {
            Buffer response = {0};
            if(command(&session, "down_one_frame", &response))
            {
                free(response.data);
                response.data = NULL;
                response.size = 0;
                if(eval_once(&session, "outer_value", "10", state.stop_count))
                {
                    log_text("RADDBG_ORACLE_UNWIND outer_value=10", NULL);
                    if(command(&session, "up_one_frame", &response) && eval_once(&session, "inner_value", "21", state.stop_count))
                    {
                        log_text("RADDBG_ORACLE_UNWIND inner_value=21", NULL);
                    }
                    else
                    {
                        sequence_ok = 0;
                        log_text("RADDBG_ORACLE_ERROR returning to the child frame failed", response.data);
                    }
                }
                else
                {
                    sequence_ok = 0;
                    log_text("RADDBG_ORACLE_ERROR caller-frame outer_value did not match", response.data);
                }
            }
            else
            {
                sequence_ok = 0;
            }
            free(response.data);
        }

        if(sequence_ok)
        {
            sequence_ok = wait_for_target_completion(&session);
        }
        ok = sequence_ok;
    }
    else
    {
        log_text("RADDBG_ORACLE_ERROR failed to prepare GUI session", NULL);
    }
    int cleanup_ok = cleanup_session(&session);
    ok = ok && cleanup_ok;
    fprintf(g_log != NULL ? g_log : stdout, "RADDBG_SESSION_RESULT status=%s\n", ok ? "pass" : "fail");
    fflush(g_log != NULL ? g_log : stdout);
    return ok;
}

int
main(int argc, char **argv)
{
    int exit_code = 1;
    Args args = {0};
    struct sigaction action = {0};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    g_log = stdout;
    if(parse_args(argc, argv, &args))
    {
        if(args.self_test)
        {
            exit_code = self_test() ? 0 : 1;
        }
        else if(session_test(&args))
        {
            exit_code = 0;
        }
    }
    else
    {
        fprintf(stderr,
                "usage: %s --raddbg PATH --debuggee PATH --source PATH --session-dir DIR --port U16 [--timeout-ms N]\n"
                "       %s --self-test\n",
                argv[0], argv[0]);
    }
    return exit_code;
}
