#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
typedef DWORD pid_t;
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>

/*
 * First-party C supervisor for the pinned RAD Debugger session oracle.
 *
 * Build with a trusted host C compiler: Linux -lX11; Windows
 * -lws2_32 -liphlpapi -luser32.  It does not modify or
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
    int prelaunch;
    uint64_t run_gen;
    uint64_t stop_count;
    uint64_t ip;
    uint64_t ip_voff;
    unsigned source_line;
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
    int gui_reaped;
    uint64_t main_thread_id;
    int prelaunch_reported;
#if defined(_WIN32)
    HANDLE gui_process;
    HANDLE job;
    HANDLE target_process;
    HANDLE output_files[2];
    char output_paths[2][PATH_MAX];
    int winsock_initialized;
    SOCKET ipc_socket;
#else
    Display *display;
    int gui_output_fd;
    int ipc_socket;
#endif
    Buffer gui_output;
    uint64_t deadline_ms;
    int failed;
};

static volatile sig_atomic_t g_interrupted;
static FILE *g_log;

static int drain_gui_output(Session *session);
static void log_text(const char *label, const char *text);

static void
signal_handler(int signo)
{
    (void)signo;
    g_interrupted = 1;
}

#if defined(_WIN32)
static void
log_windows_api_failure(const char *operation, unsigned long status, unsigned long error)
{
    FILE *stream = g_log != NULL ? g_log : stdout;
    fprintf(stream, "RADDBG_ORACLE_API_FAILURE operation=%s status=%lu error=%lu\n",
            operation, status, error);
    fflush(stream);
}

static void
log_windows_bounded_file(const char *label, const char *path, int tail)
{
    FILE *stream = g_log != NULL ? g_log : stdout;
    wchar_t wide[PATH_MAX];
    char data[4097];
    DWORD got = 0;
    DWORD error = 0;
    LARGE_INTEGER file_size = {0};
    int ok = 0;
    int truncated = 0;
    if(path != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, PATH_MAX) > 0)
    {
        HANDLE file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if(file != INVALID_HANDLE_VALUE)
        {
            if(GetFileSizeEx(file, &file_size))
            {
                LARGE_INTEGER offset = {0};
                if(tail && file_size.QuadPart > 4096)
                {
                    offset.QuadPart = file_size.QuadPart - 4096;
                    truncated = 1;
                }
                if(SetFilePointerEx(file, offset, NULL, FILE_BEGIN) &&
                   ReadFile(file, data, 4096, &got, NULL))
                {
                    data[got] = 0;
                    if(file_size.QuadPart - offset.QuadPart > (LONGLONG)got) truncated = 1;
                    ok = 1;
                }
                else error = GetLastError();
            }
            else error = GetLastError();
            CloseHandle(file);
        }
        else error = GetLastError();
    }
    else error = GetLastError();
    fprintf(stream, "%s path=%s status=%s error=%lu truncated=%d bytes=%lu\n",
            label, path != NULL ? path : "<null>", ok ? "read" : "unavailable",
            (unsigned long)error, truncated, (unsigned long)got);
    if(ok && got != 0)
    {
        fwrite(data, 1, got, stream);
        if(data[got - 1] != '\n') fputc('\n', stream);
    }
    fflush(stream);
}

static void
log_windows_launch_context(Session *session, const char *command_text)
{
    char control_log_path[PATH_MAX];
    int written = 0;
    fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_INITIAL_LAUNCH command=%s\n", command_text);
    fflush(g_log != NULL ? g_log : stdout);
    log_windows_bounded_file("RADDBG_ORACLE_PROJECT", session->project_path, 0);
    log_windows_bounded_file("RADDBG_ORACLE_DEBUGGEE_STDOUT", session->output_paths[0], 1);
    log_windows_bounded_file("RADDBG_ORACLE_DEBUGGEE_STDERR", session->output_paths[1], 1);
    log_text("RADDBG_ORACLE_GUI_STDOUT_CAPTURE",
             "unavailable: the Windows GUI child is launched without inherited standard handles");
    written = snprintf(control_log_path, sizeof(control_log_path), "%s/ctrl_thread.raddbg_log", session->logs_path);
    if(written >= 0 && (size_t)written < sizeof(control_log_path))
    {
        log_windows_bounded_file("RADDBG_ORACLE_APP_CONTROL_LOG", control_log_path, 1);
    }
}

static void
log_windows_failure_context(Session *session)
{
    char control_log_path[PATH_MAX];
    int written = 0;
    FILE *stream = g_log != NULL ? g_log : stdout;
    (void)drain_gui_output(session);
    fprintf(stream, "RADDBG_ORACLE_DEBUGGEE_OUTPUT_TAIL_BEGIN bytes=%zu\n", session->gui_output.size);
    if(session->gui_output.size != 0)
    {
        size_t start = session->gui_output.size > 4096 ? session->gui_output.size - 4096 : 0;
        fwrite(session->gui_output.data + start, 1, session->gui_output.size - start, stream);
        if(session->gui_output.data[session->gui_output.size - 1] != '\n') fputc('\n', stream);
    }
    fprintf(stream, "RADDBG_ORACLE_DEBUGGEE_OUTPUT_TAIL_END\n");
    written = snprintf(control_log_path, sizeof(control_log_path), "%s/ctrl_thread.raddbg_log", session->logs_path);
    if(written >= 0 && (size_t)written < sizeof(control_log_path))
    {
        log_windows_bounded_file("RADDBG_ORACLE_APP_CONTROL_LOG_TAIL", control_log_path, 1);
    }
    fflush(stream);
}

static uint64_t
monotonic_ms(void)
{
    return (uint64_t)GetTickCount64();
}

static void
windows_usleep(unsigned microseconds)
{
    Sleep((DWORD)((microseconds + 999u) / 1000u));
}
#define usleep windows_usleep
#else
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

#endif

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

/* Marker counting uses C strings, so captured process output must contain no NUL. */
static int
append_captured_output(Buffer *buffer, const char *data, size_t size)
{
    int ok = memchr(data, 0, size) == NULL && buffer_append(buffer, data, size);
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
    int roots = 0;
    int closed = 0;
    for(const char *p = text; *p != 0; p += 1)
    {
        if(closed)
        {
            if(*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ok = 0;
        }
        else if(in_string)
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
            if(depth == 0) roots += 1;
            depth += 1;
        }
        else if(*p == '}')
        {
            depth -= 1;
            if(depth < 0) ok = 0;
            else if(depth == 0) closed = 1;
        }
    }
    if(depth != 0 || in_string || roots != 1 || !closed) ok = 0;
    return ok;
}

static int
parse_state(const char *text, State *state)
{
    int ok = 0;
    char number[MAX_VALUE_BYTES];
    uint64_t running = 0;
    if(text != NULL && strlen(text) <= MAX_IPC_BYTES && strncmp(text, "state:\n{", 8) == 0 && balanced_md(text) &&
       line_key_count(text, 1, "stop_event") == 1 && line_key_count(text, 1, "locals") == 1 &&
       line_key_count(text, 1, "lines") == 1 && line_key_count(text, 1, "threads") == 1 &&
       line_key_count(text, 1, "modules") == 1)
    {
        State parsed = {0};
        if(read_field(text, 1, "running", number, sizeof(number)) && parse_u64(number, &running) && running <= 1 &&
           read_field(text, 1, "run_gen", number, sizeof(number)) && parse_u64(number, &parsed.run_gen) &&
           read_field(text, 1, "stop_count", number, sizeof(number)) && parse_u64(number, &parsed.stop_count) &&
           read_field(text, 1, "ip", number, sizeof(number)) && parse_u64(number, &parsed.ip) &&
           read_field(text, 1, "ip_voff", number, sizeof(number)) && parse_u64(number, &parsed.ip_voff) &&
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
            int stopped_valid = parsed.thread_count != 0 && parsed.first_thread_id != 0;
            char explanation[MAX_VALUE_BYTES] = {0};
            /* The pinned GUI exposes this exact empty state before a target is launched. */
            int prelaunch_valid = !parsed.running && parsed.ip == 0 && parsed.ip_voff == 0 &&
                                  parsed.module[0] == 0 && parsed.symbol[0] == 0 &&
                                  parsed.thread_count == 0 && parsed.first_thread_id == 0 &&
                                  read_field(text, 2, "explanation", explanation, sizeof(explanation)) &&
                                  strcmp(explanation, "Not running") == 0 &&
                                  strstr(text, " threads:\n {\n }\n") != NULL &&
                                  strstr(text, " modules:\n {\n }\n") != NULL;
            if(prelaunch_valid) parsed.prelaunch = 1;
            if(stopped_valid || prelaunch_valid)
            {
                *state = parsed;
                ok = 1;
            }
        }
    }
    return ok;
}

/* Eval is one flat document with the four ordered fields emitted upstream. */
static int
eval_document_shape(const char *text)
{
    int ok = strncmp(text, "eval:\n{\n", 8) == 0;
    const char *keys[] = {" expr:", " value:", " type:", " msgs:"};
    const char *line = ok ? text + 8 : text;
    for(unsigned i = 0; i < 4 && ok; i += 1)
    {
        const char *end = strchr(line, '\n');
        size_t key_size = strlen(keys[i]);
        ok = end != NULL && (size_t)(end - line) > key_size &&
             memcmp(line, keys[i], key_size) == 0;
        if(ok) line = end + 1;
    }
    if(ok)
    {
        ok = *line == '}';
        if(ok)
        {
            line += 1;
            while(*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line += 1;
            ok = *line == 0;
        }
    }
    return ok;
}

static int
parse_eval(const char *text, EvalResult *result)
{
    int ok = 0;
    if(text != NULL && strlen(text) <= MAX_IPC_BYTES && strncmp(text, "eval:\n{", 7) == 0 && balanced_md(text) && eval_document_shape(text))
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
parse_voff_range(const char *text, uint64_t *min_out, uint64_t *max_out)
{
    int ok = 0;
    const char *p = text;
    while(*p == ' ' || *p == '\t') p += 1;
    if(*p == '[')
    {
        p += 1;
        while(*p == ' ' || *p == '\t') p += 1;
        errno = 0;
        char *end = NULL;
        unsigned long long min = strtoull(p, &end, 0);
        if(errno == 0 && end != p)
        {
            p = end;
            while(*p == ' ' || *p == '\t') p += 1;
            if(*p == ',')
            {
                p += 1;
                while(*p == ' ' || *p == '\t') p += 1;
                errno = 0;
                unsigned long long max = strtoull(p, &end, 0);
                if(errno == 0 && end != p)
                {
                    p = end;
                    while(*p == ' ' || *p == '\t') p += 1;
                    if(*p == ')')
                    {
                        p += 1;
                        while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p += 1;
                        if(*p == 0 && max > min)
                        {
                            *min_out = (uint64_t)min;
                            *max_out = (uint64_t)max;
                            ok = 1;
                        }
                    }
                }
            }
        }
    }
    return ok;
}

static int
source_line_at_ip(const char *text, const char *file_name, uint64_t ip_voff, unsigned *line_num_out)
{
    int matches = 0;
    unsigned matched_line = 0;
    int in_lines = 0;
    int in_record = 0;
    char current_file[MAX_VALUE_BYTES] = {0};
    char current_line[MAX_VALUE_BYTES] = {0};
    char current_range[MAX_VALUE_BYTES] = {0};
    const char *line = text;
    if(text != NULL && file_name != NULL && file_name[0] != 0)
    {
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
                current_range[0] = 0;
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
            else if(in_lines && in_record && len >= 14 && strncmp(line, "   voff_range:", 14) == 0)
            {
                size_t value_len = len - 14;
                if(value_len < sizeof(current_range))
                {
                    memcpy(current_range, line + 14, value_len);
                    current_range[value_len] = 0;
                }
            }
            else if(in_lines && in_record && len == 3 && strncmp(line, "  }", 3) == 0)
            {
                uint64_t parsed_line = 0;
                uint64_t range_min = 0;
                uint64_t range_max = 0;
                if(current_file[0] != 0 && strcmp(path_basename(current_file), path_basename(file_name)) == 0 &&
                   parse_u64(current_line, &parsed_line) && parsed_line > 0 && parsed_line <= UINT_MAX &&
                   parse_voff_range(current_range, &range_min, &range_max) &&
                   range_min <= ip_voff && ip_voff < range_max)
                {
                    matches += 1;
                    matched_line = (unsigned)parsed_line;
                }
                in_record = 0;
            }
            line = end != NULL ? end + 1 : NULL;
        }
    }
    if(matches == 1 && line_num_out != NULL)
    {
        *line_num_out = matched_line;
    }
    return matches == 1;
}

static int
line_matches(const char *text, const char *file_name, unsigned line_num, uint64_t ip_voff)
{
    int ok = 0;
    unsigned actual_line = 0;
    if(source_line_at_ip(text, file_name, ip_voff, &actual_line))
    {
        ok = line_num == 0 || actual_line == line_num;
    }
    return ok;
}

static const char *
path_basename(const char *path)
{
    const char *result = path;
    for(const char *p = path; *p != 0; p += 1)
    {
        if(*p == '/' || *p == '\\')
        {
            result = p + 1;
        }
    }
    return result;
}

static int
module_matches(const char *module, const char *executable)
{
    const char *actual = path_basename(module);
    const char *expected = path_basename(executable);
#if defined(_WIN32)
    int matches = actual[0] != 0 && expected[0] != 0 && _stricmp(actual, expected) == 0;
#else
    int matches = actual[0] != 0 && expected[0] != 0 && strcmp(actual, expected) == 0;
#endif
    return matches;
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
        char text[8192];
        int complete_lines = 1;
        unsigned line_num = 0;
        while(fgets(text, sizeof(text), file) != NULL)
        {
            size_t length = strlen(text);
            if(length != 0 && text[length-1] != '\n' && !feof(file))
            {
                complete_lines = 0;
                break;
            }
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
        if(complete_lines && !ferror(file) && outer_count == 1 && inner_count == 1 && inner_done_count == 1 && worker_count == 1)
        {
            *lines = found;
            ok = 1;
        }
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
native_response_complete(const Buffer *output)
{
    int complete = 0;
    if(output->data != NULL && output->size != 0 && memchr(output->data, 0, output->size) == NULL)
    {
        size_t end = output->size;
        while(end > 0 && (output->data[end-1] == ' ' || output->data[end-1] == '\r' ||
                          output->data[end-1] == '\n' || output->data[end-1] == '\t')) end -= 1;
        complete = (end == 4 && memcmp(output->data, "done", 4) == 0) ||
                   (end != 0 && output->data[end-1] == '}' &&
                    (strncmp(output->data, "state:\n{", 8) == 0 || strncmp(output->data, "eval:\n{", 7) == 0) &&
                    balanced_md(output->data));
    }
    return complete;
}

/* Pinned Eval emits six strings joined with five NUL separators.  Validate
 * their exact boundaries before concatenating; NUL inside a field is invalid. */
static int
normalize_native_response(const Buffer *raw, Buffer *output)
{
    int ok = 0;
    Buffer joined = {0};
    if(raw != NULL && raw->data != NULL && raw->size != 0 && raw->size <= MAX_IPC_BYTES &&
       output != NULL && output->data == NULL && output->size == 0)
    {
        const char *separator = memchr(raw->data, 0, raw->size);
        if(separator == NULL)
        {
            EvalResult eval = {0};
            if(native_response_complete(raw) &&
               (strncmp(raw->data, "eval:\n{", 7) != 0 || parse_eval(raw->data, &eval)))
            {
                ok = buffer_append(&joined, raw->data, raw->size);
            }
        }
        else
        {
            const char *parts[6] = {0};
            size_t sizes[6] = {0};
            size_t offset = 0;
            int valid = 1;
            for(unsigned i = 0; i < 6 && valid; i += 1)
            {
                size_t remaining = raw->size - offset;
                const char *end = memchr(raw->data + offset, 0, remaining);
                parts[i] = raw->data + offset;
                sizes[i] = end != NULL ? (size_t)(end - parts[i]) : remaining;
                valid = i < 5 ? end != NULL : end == NULL;
                if(valid) offset += sizes[i] + (i < 5 ? 1u : 0u);
            }
            const char *prefixes[] = {" expr:  ", " value: \"", " type:  \"", " msgs:  \""};
            if(valid)
            {
                valid = sizes[0] == 8 && memcmp(parts[0], "eval:\n{\n", 8) == 0 &&
                        sizes[5] == 2 && memcmp(parts[5], "}\n", 2) == 0;
            }
            for(unsigned i = 1; i < 5 && valid; i += 1)
            {
                size_t prefix_size = strlen(prefixes[i-1]);
                valid = sizes[i] > prefix_size && memcmp(parts[i], prefixes[i-1], prefix_size) == 0 &&
                        parts[i][sizes[i]-1] == '\n' && memchr(parts[i], '\n', sizes[i]-1) == NULL &&
                        (i == 1 || parts[i][sizes[i]-2] == '"');
            }
            for(unsigned i = 0; i < 6 && valid; i += 1)
            {
                valid = buffer_append(&joined, parts[i], sizes[i]);
            }
            EvalResult eval = {0};
            ok = valid && native_response_complete(&joined) && parse_eval(joined.data, &eval);
        }
    }
    if(ok) *output = joined;
    else free(joined.data);
    return ok;
}

#if defined(_WIN32)
/* Pinned win32_socket.c sends raw TCP bytes; its five-u64 headers belong to
 * internal rings, not the wire.  Use the native protocol directly because the
 * pinned --cli sender reopens stdout/stderr as CONOUT$, discarding pipes. */
static int
gui_alive(Session *session)
{
    int alive = session->gui_process != NULL &&
                WaitForSingleObject(session->gui_process, 0) == WAIT_TIMEOUT;
    if(!alive && session->gui_process != NULL) session->gui_reaped = 1;
    return alive;
}

static int
drain_gui_output(Session *session)
{
    int ok = !session->failed;
    if(session->failed)
    {
        log_text("RADDBG_ORACLE_ERROR target output drain rejected after a prior session failure", NULL);
    }
    for(unsigned i = 0; i < 2 && ok; i += 1)
    {
        if(session->output_files[i] == NULL && session->output_paths[i][0] != 0)
        {
            wchar_t path[PATH_MAX];
            if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, session->output_paths[i], -1, path, PATH_MAX) <= 0)
            {
                DWORD error = GetLastError();
                log_windows_api_failure("MultiByteToWideChar output path", 0, error);
                ok = 0;
            }
            else
            {
                HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if(file != INVALID_HANDLE_VALUE) session->output_files[i] = file;
                else
                {
                    DWORD error = GetLastError();
                    if(error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND && error != ERROR_SHARING_VIOLATION)
                    {
                        log_windows_api_failure("CreateFileW target output", 0, error);
                        ok = 0;
                    }
                }
            }
        }
        int reading = session->output_files[i] != NULL;
        while(reading && ok)
        {
            char chunk[8192];
            DWORD got = 0;
            if(!ReadFile(session->output_files[i], chunk, sizeof(chunk), &got, NULL))
            {
                DWORD error = GetLastError();
                log_windows_api_failure("ReadFile target output", 0, error);
                ok = 0;
            }
            else if(got == 0)
            {
                reading = 0;
            }
            else if(!append_captured_output(&session->gui_output, chunk, (size_t)got))
            {
                log_text("RADDBG_ORACLE_ERROR target stdout/stderr contains NUL or exceeds 1 MiB cap", NULL);
                ok = 0;
            }
        }
    }
    if(!ok) session->failed = 1;
    return ok;
}

/* Reject ambiguous ownership as well as a foreign listener on this port.
 * Query both address families even though native IPC connects to IPv4. */
static int
tcp_listener_owner(uint16_t port, DWORD *pid_out)
{
    int result = 0;
    DWORD owner = 0;
    ULONG families[2] = {AF_INET, AF_INET6};
    for(unsigned family_idx = 0; family_idx < 2 && result >= 0; family_idx += 1)
    {
        DWORD bytes = 0;
        DWORD queried = GetExtendedTcpTable(NULL, &bytes, FALSE, families[family_idx],
                                           TCP_TABLE_OWNER_PID_LISTENER, 0);
        void *table = queried == ERROR_INSUFFICIENT_BUFFER && bytes <= MAX_IPC_BYTES ? malloc(bytes) : NULL;
        if(table == NULL)
        {
            result = -1;
        }
        else
        {
            queried = GetExtendedTcpTable(table, &bytes, FALSE, families[family_idx],
                                          TCP_TABLE_OWNER_PID_LISTENER, 0);
            if(queried != NO_ERROR)
            {
                result = -1;
            }
            else
            {
                DWORD count = families[family_idx] == AF_INET ?
                              ((MIB_TCPTABLE_OWNER_PID *)table)->dwNumEntries :
                              ((MIB_TCP6TABLE_OWNER_PID *)table)->dwNumEntries;
                for(DWORD i = 0; i < count && result >= 0; i += 1)
                {
                    DWORD local_port = 0;
                    DWORD pid = 0;
                    if(families[family_idx] == AF_INET)
                    {
                        MIB_TCPROW_OWNER_PID *row = &((MIB_TCPTABLE_OWNER_PID *)table)->table[i];
                        local_port = row->dwLocalPort;
                        pid = row->dwOwningPid;
                    }
                    else
                    {
                        MIB_TCP6ROW_OWNER_PID *row = &((MIB_TCP6TABLE_OWNER_PID *)table)->table[i];
                        local_port = row->dwLocalPort;
                        pid = row->dwOwningPid;
                    }
                    if(ntohs((u_short)local_port) == port)
                    {
                        if(result != 0 && owner != pid) result = -1;
                        else
                        {
                            owner = pid;
                            result = 1;
                        }
                    }
                }
            }
            free(table);
        }
    }
    if(result == 1) *pid_out = owner;
    return result;
}

static int
process_owns_ipc_listener(Session *session)
{
    DWORD owner = 0;
    int owns = gui_alive(session) && tcp_listener_owner(session->args.port, &owner) == 1 &&
               owner == session->gui_pid;
    return owns;
}

static int
wait_for_owned_ipc(Session *session)
{
    int ok = 0;
    while(monotonic_ms() < session->deadline_ms && !g_interrupted && !session->failed)
    {
        DWORD owner = 0;
        int listener = tcp_listener_owner(session->args.port, &owner);
        if(listener < 0)
        {
            log_text("RADDBG_ORACLE_ERROR cannot inspect Windows TCP listener ownership", NULL);
            break;
        }
        if(listener == 1)
        {
            ok = owner == session->gui_pid && gui_alive(session);
            if(!ok) log_text("RADDBG_ORACLE_ERROR IPC port is owned by another process", NULL);
            break;
        }
        if(!gui_alive(session))
        {
            log_text("RADDBG_ORACLE_ERROR GUI exited before owning IPC listener", NULL);
            break;
        }
        drain_gui_output(session);
        Sleep(POLL_INTERVAL_MS);
    }
    if(!ok) log_text("RADDBG_ORACLE_ERROR child-owned IPC listener not observed before deadline", NULL);
    return ok;
}

static int
socket_ready(SOCKET socket_handle, int writing, uint64_t deadline)
{
    int ready = 0;
    if(monotonic_ms() < deadline && !g_interrupted)
    {
        fd_set read_set;
        fd_set write_set;
        fd_set error_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        FD_SET(socket_handle, &error_set);
        if(writing) FD_SET(socket_handle, &write_set);
        else FD_SET(socket_handle, &read_set);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;
        int selected = select(0, &read_set, &write_set, &error_set, &timeout);
        if(selected == SOCKET_ERROR || (selected > 0 && FD_ISSET(socket_handle, &error_set))) ready = -1;
        else if(selected > 0) ready = 1;
    }
    return ready;
}

static int
run_ipc(Session *session, const char *command_text, Buffer *output)
{
    int ok = 0;
    Buffer raw_response = {0};
    SOCKET client = INVALID_SOCKET;
    uint64_t end = monotonic_ms() + 10000u;
    if(end > session->deadline_ms) end = session->deadline_ms;
    int fresh_connection = 0;
    if(!session->failed && process_owns_ipc_listener(session))
    {
        client = session->ipc_socket;
        if(client == INVALID_SOCKET)
        {
            client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            session->ipc_socket = client;
            fresh_connection = 1;
        }
    }
    if(client != INVALID_SOCKET)
    {
        u_long nonblocking = 1;
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(session->args.port);
        int connected = !fresh_connection;
        if(fresh_connection && ioctlsocket(client, FIONBIO, &nonblocking) == 0)
        {
            int status = connect(client, (const struct sockaddr *)&address, sizeof(address));
            int error = status == SOCKET_ERROR ? WSAGetLastError() : 0;
            if(status == 0) connected = 1;
            else if(error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
            {
                while(monotonic_ms() < end && !g_interrupted)
                {
                    int ready = socket_ready(client, 1, end);
                    if(ready < 0) break;
                    if(ready == 1)
                    {
                        int socket_error = 0;
                        int size = sizeof(socket_error);
                        connected = getsockopt(client, SOL_SOCKET, SO_ERROR, (char *)&socket_error, &size) == 0 &&
                                    socket_error == 0;
                        break;
                    }
                }
            }
        }
        size_t command_size = strlen(command_text);
        int sent = 0;
        if(connected && command_size != 0 && command_size < 65536u && process_owns_ipc_listener(session))
        {
            while(monotonic_ms() < end && !g_interrupted)
            {
                int ready = socket_ready(client, 1, end);
                if(ready < 0) break;
                if(ready == 1)
                {
                    /* Upstream consumes each recv as one command: refuse a partial
                     * send rather than execute two unintended partial commands. */
                    sent = send(client, command_text, (int)command_size, 0) == (int)command_size;
                    break;
                }
            }
        }
        uint64_t last_byte_ms = 0;
        while(sent && monotonic_ms() < end && !g_interrupted && !session->failed)
        {
            if(!drain_gui_output(session) || !gui_alive(session)) break;
            int ready = socket_ready(client, 0, end);
            if(ready < 0) break;
            if(ready == 1)
            {
                char chunk[8192];
                int got = recv(client, chunk, sizeof(chunk), 0);
                if(got <= 0)
                {
                    if(got == 0) log_text("RADDBG_ORACLE_ERROR RAD IPC connection closed mid-session", NULL);
                    break;
                }
                if(!buffer_append(&raw_response, chunk, (size_t)got))
                {
                    log_text("RADDBG_ORACLE_ERROR oversized native IPC response", NULL);
                    break;
                }
                last_byte_ms = monotonic_ms();
            }
            else if(last_byte_ms != 0 && monotonic_ms() - last_byte_ms >= 200u && normalize_native_response(&raw_response, output))
            {
                ok = process_owns_ipc_listener(session);
                break;
            }
        }
    }
    if(!ok)
    {
        session->failed = 1;
        log_text("RADDBG_ORACLE_ERROR native IPC failed, incomplete, or timed out", NULL);
    }
    free(raw_response.data);
    return ok;
}

static int
utf8_to_wide(const char *text, wchar_t *wide, size_t capacity)
{
    int ok = capacity <= INT_MAX && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                                                       wide, (int)capacity) > 0;
    return ok;
}

static int
identify_target(Session *session, uint64_t thread_id)
{
    int ok = 0;
    HANDLE thread = thread_id > 0 && thread_id <= UINT32_MAX ?
                    OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)thread_id) : NULL;
    if(thread != NULL)
    {
        DWORD pid = GetProcessIdOfThread(thread);
        HANDLE process = pid != 0 ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid) : NULL;
        if(process != NULL)
        {
            wchar_t actual[PATH_MAX];
            wchar_t expected[PATH_MAX];
            wchar_t absolute[PATH_MAX];
            DWORD size = PATH_MAX;
            DWORD absolute_size = 0;
            BOOL in_job = FALSE;
            if(utf8_to_wide(session->args.debuggee, expected, PATH_MAX))
                absolute_size = GetFullPathNameW(expected, PATH_MAX, absolute, NULL);
            if(absolute_size != 0 && absolute_size < PATH_MAX &&
               QueryFullProcessImageNameW(process, 0, actual, &size) && _wcsicmp(actual, absolute) == 0 &&
               IsProcessInJob(process, session->job, &in_job) && in_job)
            {
                session->target_pid = pid;
                session->target_process = process;
                ok = 1;
            }
            else CloseHandle(process);
        }
        CloseHandle(thread);
    }
    if(!ok) log_text("RADDBG_ORACLE_ERROR stopped thread does not identify the owned fixture process", NULL);
    return ok;
}

#else
static int
drain_gui_output(Session *session)
{
    int ok = !session->failed;
    if(session->gui_output_fd >= 0)
    {
        int reading = 1;
        while(reading && ok)
        {
            char chunk[8192];
            ssize_t got = read(session->gui_output_fd, chunk, sizeof(chunk));
            if(got > 0)
            {
                if(!append_captured_output(&session->gui_output, chunk, (size_t)got))
                {
                    session->failed = 1;
                    ok = 0;
                    log_text("RADDBG_ORACLE_ERROR captured GUI/debuggee output contains NUL or exceeds 1 MiB cap", NULL);
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
reap_child_bounded(pid_t child, int *status_out, uint32_t timeout_ms)
{
    int reaped = 0;
    uint64_t end = monotonic_ms() + timeout_ms;
    while(monotonic_ms() < end && !reaped)
    {
        int status = 0;
        pid_t waited = waitpid(child, &status, WNOHANG);
        if(waited == child)
        {
            *status_out = status;
            reaped = 1;
        }
        else if(waited < 0 && errno != EINTR)
        {
            break;
        }
        else
        {
            usleep(10 * 1000);
        }
    }
    return reaped;
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
                int fields = sscanf(line, " %u: %79s %79s %7s %79s %79s %79s %u %lu %lu",
                                    &slot, local, remote, state, queue, timers, retransmits, &uid, &timeout, &inode);
                char *colon = strchr(local, ':');
                if(fields == 10 && colon != NULL)
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
    while(monotonic_ms() < session->deadline_ms && !g_interrupted && !session->failed)
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
        pid_t waited = session->gui_pid > 0 ? waitpid(session->gui_pid, &status, WNOHANG) : -1;
        if(session->gui_pid <= 0 || waited == session->gui_pid)
        {
            session->gui_reaped = 1;
            fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_ERROR GUI exited before owning IPC port, status=%d\n", status);
            break;
        }
        else if(waited < 0 && errno != EINTR)
        {
            log_text("RADDBG_ORACLE_ERROR could not inspect GUI child status", NULL);
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

#define LINUX_IPC_TIMEOUT_MS 10000u
#define LINUX_IPC_POLL_MS 50
#define LINUX_IPC_COMMAND_CAP 65536u

static int
linux_socket_ready(int fd, short events, uint64_t deadline_ms)
{
    int result = 0;
    while(monotonic_ms() < deadline_ms && !g_interrupted)
    {
        uint64_t now = monotonic_ms();
        if(now >= deadline_ms) break;
        uint64_t remaining = deadline_ms - now;
        int timeout_ms = remaining > LINUX_IPC_POLL_MS ? LINUX_IPC_POLL_MS : (int)remaining;
        struct pollfd pfd = {0};
        pfd.fd = fd;
        pfd.events = events;
        int polled = poll(&pfd, 1, timeout_ms);
        if(polled > 0)
        {
            if((pfd.revents & events) != 0)
            {
                result = 1;
            }
            else if((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            {
                result = -1;
            }
            break;
        }
        if(polled == 0) break;
        if(polled < 0 && errno != EINTR)
        {
            result = -1;
            break;
        }
    }
    return result;
}

static int
linux_gui_alive(Session *session)
{
    int alive = 0;
    if(session->gui_pid > 0 && !session->gui_reaped)
    {
        int status = 0;
        pid_t waited = -1;
        do
        {
            waited = waitpid(session->gui_pid, &status, WNOHANG);
        }
        while(waited < 0 && errno == EINTR);
        if(waited == 0)
        {
            alive = 1;
        }
        else if(waited == session->gui_pid)
        {
            session->gui_reaped = 1;
        }
        else
        {
            log_text("RADDBG_ORACLE_ERROR could not verify owned GUI liveness", NULL);
        }
    }
    return alive;
}

static int
run_ipc(Session *session, const char *command_text, Buffer *output)
{
    int ok = 0;
    int was_failed = session->failed;
    Buffer raw_response = {0};
    uint64_t deadline_ms = monotonic_ms() + LINUX_IPC_TIMEOUT_MS;
    if(deadline_ms > session->deadline_ms) deadline_ms = session->deadline_ms;
    int permitted = !session->failed && output != NULL && output->size == 0 && command_text != NULL &&
                    linux_gui_alive(session) && process_owns_ipc_listener(session);
    int fresh_connection = session->ipc_socket < 0;
    if(permitted && fresh_connection)
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if(fd < 0) permitted = 0;
        else
        {
            int status_flags = fcntl(fd, F_GETFL, 0);
            int descriptor_flags = fcntl(fd, F_GETFD, 0);
            if(status_flags >= 0 && descriptor_flags >= 0 &&
               fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) == 0 &&
               fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) == 0)
            {
                session->ipc_socket = fd;
            }
            else
            {
                close(fd);
                permitted = 0;
            }
        }
    }
    int fd = session->ipc_socket;
    int connected = permitted && !fresh_connection;
    if(permitted && fresh_connection)
    {
        struct sockaddr_in address = {0};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(session->args.port);
        int connect_result = connect(fd, (const struct sockaddr *)&address, sizeof(address));
        if(connect_result == 0) connected = 1;
        else if(errno == EINPROGRESS || errno == EALREADY || errno == EINTR)
        {
            while(monotonic_ms() < deadline_ms && !g_interrupted && !session->failed)
            {
                int ready = linux_socket_ready(fd, POLLOUT, deadline_ms);
                if(ready < 0) break;
                if(ready == 1)
                {
                    int socket_error = 0;
                    socklen_t error_size = sizeof(socket_error);
                    connected = getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) == 0 &&
                                socket_error == 0;
                    break;
                }
            }
        }
    }
    size_t command_size = command_text != NULL ? strlen(command_text) : 0;
    int runnable = permitted && connected && command_size > 0 && command_size < LINUX_IPC_COMMAND_CAP &&
                   linux_gui_alive(session) && process_owns_ipc_listener(session);
    /* Do not retry a positive short write: the pinned server consumes each
     * recv as a complete command. EINTR/EAGAIN with no bytes may be retried. */
    int sent = 0;
    while(runnable && monotonic_ms() < deadline_ms && !g_interrupted && !sent)
    {
        int ready = linux_socket_ready(fd, POLLOUT, deadline_ms);
        if(ready < 0) break;
        if(ready == 1)
        {
            ssize_t written = send(fd, command_text, command_size, MSG_NOSIGNAL);
            if(written == (ssize_t)command_size) sent = 1;
            else if(written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            {
                continue;
            }
            else break;
        }
    }
    uint64_t last_byte_ms = 0;
    while(sent && monotonic_ms() < deadline_ms && !g_interrupted && !session->failed)
    {
        if(!drain_gui_output(session) || !linux_gui_alive(session)) break;
        int ready = linux_socket_ready(fd, POLLIN, deadline_ms);
        if(ready < 0) break;
        if(ready == 1)
        {
            char chunk[8192];
            ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
            if(got > 0)
            {
                if(!buffer_append(&raw_response, chunk, (size_t)got))
                {
                    log_text("RADDBG_ORACLE_ERROR oversized native IPC response", NULL);
                    break;
                }
                last_byte_ms = monotonic_ms();
            }
            else if(got == 0)
            {
                log_text("RADDBG_ORACLE_ERROR RAD IPC connection closed mid-session", NULL);
                break;
            }
            else if(errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) break;
        }
        else if(last_byte_ms != 0 && monotonic_ms() - last_byte_ms >= 200u && normalize_native_response(&raw_response, output))
        {
            ok = linux_gui_alive(session) && process_owns_ipc_listener(session);
            break;
        }
    }
    if(!ok)
    {
        if(!was_failed)
        {
            int errno_snapshot = errno;
            int gui_alive_after = linux_gui_alive(session);
            unsigned long listener_inode = 0;
            int listener_state = tcp_listener_inode(session->args.port, &listener_inode);
            int listener_owned = listener_state == 1 && process_owns_socket_inode(session->gui_pid, listener_inode);
            int command_length = 0;
            const char *command = command_text != NULL ? command_text : "";
            while(command_length < 80 && command[command_length] != 0) command_length += 1;
            fprintf(g_log != NULL ? g_log : stdout,
                    "RADDBG_ORACLE_IPC_DIAGNOSTIC command=%.*s sent=%d response_bytes=%zu last_byte=%llu gui_alive=%d gui_reaped=%d listener_state=%d listener_inode=%lu listener_owned=%d socket=%d errno_snapshot=%d\n",
                    command_length, command, sent, raw_response.size, (unsigned long long)last_byte_ms,
                    gui_alive_after, session->gui_reaped, listener_state, listener_inode, listener_owned,
                    session->ipc_socket, errno_snapshot);
            if(raw_response.size != 0)
            {
                size_t preview = raw_response.size < 64 ? raw_response.size : 64;
                fputs("RADDBG_ORACLE_IPC_RAW_HEX ", g_log != NULL ? g_log : stdout);
                for(size_t i = 0; i < preview; i += 1)
                {
                    fprintf(g_log != NULL ? g_log : stdout, "%02x", (unsigned char)raw_response.data[i]);
                }
                fputc('\n', g_log != NULL ? g_log : stdout);
            }
            if(session->gui_output.size != 0)
            {
                size_t preview = session->gui_output.size < 4096 ? session->gui_output.size : 4096;
                size_t start = session->gui_output.size - preview;
                fputs("RADDBG_ORACLE_GUI_OUTPUT_TAIL_BEGIN\n", g_log != NULL ? g_log : stdout);
                fwrite(session->gui_output.data + start, 1, preview, g_log != NULL ? g_log : stdout);
                if(session->gui_output.data[session->gui_output.size - 1] != '\n')
                    fputc('\n', g_log != NULL ? g_log : stdout);
                fputs("RADDBG_ORACLE_GUI_OUTPUT_TAIL_END\n", g_log != NULL ? g_log : stdout);
            }
            fflush(g_log != NULL ? g_log : stdout);
        }
        session->failed = 1;
        log_text("RADDBG_ORACLE_ERROR native IPC failed, incomplete, or timed out", NULL);
    }
    free(raw_response.data);
    return ok;
}

/* Invoke after cleanup has terminated/reaped the GUI. This deliberately does
 * not close an open connection while the pinned GUI may still be listening. */
static int
linux_close_ipc_socket_after_gui(Session *session)
{
    int ok = 1;
    if(session->ipc_socket >= 0)
    {
        if(session->gui_pid > 0 && !session->gui_reaped)
        {
            log_text("RADDBG_ORACLE_ERROR refusing to close IPC before GUI is reaped", NULL);
            ok = 0;
        }
        else
        {
            if(close(session->ipc_socket) != 0)
            {
                log_text("RADDBG_ORACLE_ERROR failed to close native IPC socket", NULL);
                ok = 0;
            }
            session->ipc_socket = -1;
        }
    }
    return ok;
}

#endif

static void
log_command_result(const char *text, const Buffer *response)
{
    fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_COMMAND %s\n", text);
    fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_RESPONSE_BEGIN\n%s\nRADDBG_ORACLE_RESPONSE_END\n",
            response->data != NULL ? response->data : "<no response>");
    fflush(g_log != NULL ? g_log : stdout);
}

static int
command(Session *session, const char *text, Buffer *response)
{
    int ok = run_ipc(session, text, response);
    log_command_result(text, response);
    return ok && response->size != 0;
}

static int
state_query_raw(Session *session, State *state, Buffer *response_out)
{
    int ok = 0;
    Buffer response = {0};
    int command_ok = run_ipc(session, "state", &response);
    int response_ok = command_ok && response.size != 0;
    if(response_ok) ok = parse_state(response.data, state);
    if(ok && state->prelaunch)
    {
        if(!session->prelaunch_reported) log_command_result("state", &response);
        session->prelaunch_reported = 1;
    }
    else
    {
        log_command_result("state", &response);
        session->prelaunch_reported = 0;
        if(response_ok && !ok)
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
    while(monotonic_ms() < session->deadline_ms && !g_interrupted && !session->failed)
    {
        State state = {0};
        Buffer response = {0};
        if(state_query_raw(session, &state, &response))
        {
            if(!state.prelaunch && !state.running && state.stop_count > old_stop_count && state.run_gen > old_run_gen)
            {
                uint64_t selected_id = 0;
                int source_location_ok = source_line_at_ip(response.data, session->args.source, state.ip_voff, &state.source_line);
                int expected_line_ok = source_location_ok && (expected_line == 0 || state.source_line == expected_line);
                int location_ok = selected_thread_id(&state, &selected_id) &&
                                  module_matches(state.module, session->args.debuggee) &&
                                  symbol_matches(state.symbol, expected_symbol) &&
                                  expected_line_ok;
                if(location_ok)
                {
#if defined(_WIN32)
                    if(session->target_pid == 0)
                    {
                        identify_target(session, selected_id);
                    }
#else
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
#endif
                    if(session->target_pid != 0)
                    {
                        if(session->main_thread_id == 0) session->main_thread_id = selected_id;
                        *state_out = state;
                        ok = 1;
                    }
                }
                else
                {
                    fprintf(g_log != NULL ? g_log : stdout,
                            "RADDBG_ORACLE_ERROR stopped at module '%s' symbol '%s' (wanted module '%s' symbol '%s' line %u)\n",
                            state.module, state.symbol, path_basename(session->args.debuggee), expected_symbol, expected_line);
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
#if defined(_WIN32)
        if(!gui_alive(session))
        {
            log_text("RADDBG_ORACLE_ERROR GUI exited while awaiting stop", NULL);
            break;
        }
#else
        int status = 0;
        pid_t waited = session->gui_pid > 0 ? waitpid(session->gui_pid, &status, WNOHANG) : 0;
        if(waited == session->gui_pid && session->gui_pid > 0)
        {
            session->gui_reaped = 1;
            fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_ERROR GUI exited while awaiting stop, status=%d\n", status);
            break;
        }
        else if(waited < 0 && errno != EINTR)
        {
            log_text("RADDBG_ORACLE_ERROR could not inspect GUI child status", NULL);
            break;
        }
#endif
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
selected_thread_is(const State *state, uint64_t expected_thread_id)
{
    uint64_t selected_thread_id_value = 0;
    int ok = expected_thread_id != 0 && selected_thread_id(state, &selected_thread_id_value) &&
             selected_thread_id_value == expected_thread_id;
    return ok;
}

static int
step_over_to_inner_marker(Session *session, State *state, unsigned marker_line, uint64_t expected_thread_id)
{
    int ok = 0;
    State next = {0};
    char command_text[PATH_MAX + 128];
    unsigned before_line = state->source_line;
    uint64_t before_ip = state->ip_voff;
    if(marker_line != 0 && before_line != 0 && before_line <= marker_line &&
       selected_thread_is(state, expected_thread_id) &&
       send_and_wait(session, "step_over", state, "debuggee_inner", 0, &next) &&
       selected_thread_is(&next, expected_thread_id) &&
       next.source_line > before_line && next.source_line <= marker_line &&
       next.ip_voff > before_ip)
    {
        *state = next;
        if(state->source_line == marker_line)
        {
            ok = 1;
        }
        else if(state->source_line < marker_line)
        {
            snprintf(command_text, sizeof(command_text), "run_to_line %s:%u", session->args.source, marker_line);
            if(send_and_wait(session, command_text, state, "debuggee_inner", marker_line, &next) &&
               selected_thread_is(&next, expected_thread_id) &&
               next.source_line == marker_line && next.ip_voff > state->ip_voff)
            {
                *state = next;
                ok = 1;
            }
        }
    }
    if(!ok)
    {
        fprintf(g_log != NULL ? g_log : stdout,
                "RADDBG_ORACLE_ERROR inner step failed to reach marker monotonically before_line=%u after_line=%u marker_line=%u before_ip=0x%llx after_ip=0x%llx thread=%llu\n",
                before_line, state->source_line, marker_line, (unsigned long long)before_ip,
                (unsigned long long)state->ip_voff, (unsigned long long)expected_thread_id);
    }
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
            line = line_end < end ? line_end + 1 : NULL;
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
eval_once(Session *session, const char *expr, const char *expected, uint64_t stop_count, EvalResult *observation)
{
    int ok = 0;
    if(observation != NULL) memset(observation, 0, sizeof(*observation));
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
        if(eval_parsed && observation != NULL) *observation = eval;
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
state_matches_verified_stop(const State *actual, const State *verified, uint64_t expected_thread_id)
{
    uint64_t actual_thread_id = 0;
    uint64_t verified_thread_id = 0;
    int result = !actual->running && !verified->running &&
                 actual->run_gen == verified->run_gen && actual->stop_count == verified->stop_count &&
                 actual->ip == verified->ip && actual->ip_voff == verified->ip_voff &&
                 strcmp(actual->module, verified->module) == 0 &&
                 strcmp(actual->symbol, verified->symbol) == 0 &&
                 selected_thread_id(actual, &actual_thread_id) &&
                 selected_thread_id(verified, &verified_thread_id) &&
                 actual_thread_id == expected_thread_id && verified_thread_id == expected_thread_id;
    return result;
}

static void
diagnostic_eval_variants(Session *session, const State *verified, const char *original_expr)
{
    int allowed = strcmp(original_expr, "record.samples[0]") == 0;
    uint64_t expected_thread_id = 0;
    int ok = allowed && selected_thread_id(verified, &expected_thread_id);
    char parenthesized[MAX_VALUE_BYTES * 2];
    char arithmetic[MAX_VALUE_BYTES * 2];
    char dereferenced[MAX_VALUE_BYTES * 2];
    snprintf(parenthesized, sizeof(parenthesized), "(%s)", original_expr);
    snprintf(arithmetic, sizeof(arithmetic), "(%s) + 0", original_expr);
    snprintf(dereferenced, sizeof(dereferenced), "*(&%s)", original_expr);
    const char *probes[] = {parenthesized, arithmetic, dereferenced};
    fprintf(g_log != NULL ? g_log : stdout,
            "RADDBG_ORACLE_EVAL_DIAGNOSTIC_BEGIN original=%s stop_count=%llu run_gen=%llu thread=%llu\n",
            original_expr, (unsigned long long)verified->stop_count, (unsigned long long)verified->run_gen,
            (unsigned long long)expected_thread_id);
    for(size_t i = 0; i < sizeof(probes) / sizeof(probes[0]) && ok && !session->failed; i += 1)
    {
        State before = {0};
        State after = {0};
        Buffer before_response = {0};
        Buffer eval_response = {0};
        Buffer after_response = {0};
        EvalResult eval = {0};
        int before_ok = state_query_raw(session, &before, &before_response);
        int same_before = before_ok && state_matches_verified_stop(&before, verified, expected_thread_id);
        int command_ok = same_before && command(session, probes[i], &eval_response);
        int eval_parsed = command_ok && parse_eval(eval_response.data, &eval);
        int after_ok = command_ok && state_query_raw(session, &after, &after_response);
        int same_after = after_ok && state_matches_verified_stop(&after, verified, expected_thread_id);
        fprintf(g_log != NULL ? g_log : stdout,
                "RADDBG_ORACLE_EVAL_DIAGNOSTIC_PROBE index=%zu expr=%s before_stable=%d parsed=%d after_stable=%d value=%s type=%s msgs=%s\n",
                i, probes[i], same_before, eval_parsed, same_after,
                eval_parsed ? eval.value : "", eval_parsed ? eval.type : "",
                eval_parsed ? eval.messages : "");
        fflush(g_log != NULL ? g_log : stdout);
        free(before_response.data);
        free(eval_response.data);
        free(after_response.data);
        if(!same_before || !command_ok || !same_after) ok = 0;
    }
    fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_EVAL_DIAGNOSTIC_END status=%s\n", ok ? "complete" : "incomplete");
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
    for(size_t i = 0; i < count && ok; i += 1)
    {
        char base[MAX_VALUE_BYTES];
        size_t out = 0;
        while(values[i].expr[out] != 0 && values[i].expr[out] != '[' && values[i].expr[out] != '.' && values[i].expr[out] != '-' && out + 1 < sizeof(base))
        {
            base[out] = values[i].expr[out];
            out += 1;
        }
        base[out] = 0;
        int local_ok = base[0] != 0 && has_local(response.data != NULL ? response.data : "", base);
        if(!local_ok)
        {
            ok = 0;
        }
        else
        {
            EvalResult observation = {0};
            int eval_ok = eval_once(session, values[i].expr, values[i].value, state->stop_count, &observation);
            if(!eval_ok)
            {
                if(strcmp(values[i].expr, "record.samples[0]") == 0 &&
                   strcmp(observation.value, "int32") == 0 && strcmp(observation.type, "int32") == 0 &&
                   observation.messages[0] == 0)
                {
                    diagnostic_eval_variants(session, state, values[i].expr);
                }
                ok = 0;
            }
        }
    }
    free(response.data);
    return ok;
}

#if defined(_WIN32)
typedef struct WindowSearch WindowSearch;
struct WindowSearch
{
    DWORD pid;
    int found;
    unsigned visited;
};

static BOOL CALLBACK
find_owned_window(HWND window, LPARAM parameter)
{
    WindowSearch *search = (WindowSearch *)parameter;
    DWORD pid = 0;
    RECT rect;
    GetWindowThreadProcessId(window, &pid);
    search->visited += 1;
    if(pid == search->pid && IsWindowVisible(window) && GetWindowRect(window, &rect) &&
       rect.right > rect.left && rect.bottom > rect.top)
    {
        search->found = 1;
    }
    return !search->found && search->visited < MAX_X11_NODES;
}

static int
wait_for_gui_window(Session *session)
{
    int ok = 0;
    while(monotonic_ms() < session->deadline_ms && !g_interrupted && !session->failed)
    {
        WindowSearch search = {0};
        search.pid = session->gui_pid;
        EnumWindows(find_owned_window, (LPARAM)&search);
        if(search.found && gui_alive(session))
        {
            ok = 1;
            break;
        }
        if(!gui_alive(session))
        {
            log_text("RADDBG_ORACLE_ERROR GUI exited before displaying its own window", NULL);
            break;
        }
        drain_gui_output(session);
        Sleep(POLL_INTERVAL_MS);
    }
    if(!ok) log_text("RADDBG_ORACLE_ERROR visible child-owned RAD window not observed before deadline", NULL);
    return ok;
}

#else
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
        while(monotonic_ms() < session->deadline_ms && !g_interrupted && !session->failed)
        {
            if(window_class_visible(session->display, DefaultRootWindow(session->display)))
            {
                ok = 1;
                break;
            }
            int status = 0;
            pid_t waited = session->gui_pid > 0 ? waitpid(session->gui_pid, &status, WNOHANG) : 0;
            if(waited == session->gui_pid && session->gui_pid > 0)
            {
                session->gui_reaped = 1;
                fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_ERROR GUI exited before mapping window, status=%d\n", status);
                break;
            }
            else if(waited < 0 && errno != EINTR)
            {
                log_text("RADDBG_ORACLE_ERROR could not inspect GUI child status", NULL);
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

#endif

static int
format_path(char *dst, size_t dst_cap, const char *format, const char *path)
{
    int written = snprintf(dst, dst_cap, format, path);
    return written >= 0 && (size_t)written < dst_cap;
}

#if defined(_WIN32)
/* Always quote each Windows argument, doubling backslashes before quotes and
 * before the closing quote according to the CRT command-line grammar. */
static int
append_windows_arg(wchar_t *command_line, size_t capacity, size_t *used, const char *argument)
{
    int ok = 0;
    wchar_t wide[PATH_MAX + 64];
    if(utf8_to_wide(argument, wide, sizeof(wide) / sizeof(wide[0])))
    {
        size_t needed = *used + 3;
        for(const wchar_t *p = wide; *p != 0; p += 1)
        {
            if(*p == L'\\')
            {
                size_t slashes = 0;
                while(p[slashes] == L'\\') slashes += 1;
                needed += slashes * ((p[slashes] == L'"' || p[slashes] == 0) ? 2u : 1u);
                p += slashes - 1;
            }
            else needed += *p == L'"' ? 2u : 1u;
        }
        if(needed < capacity)
        {
            if(*used != 0) command_line[(*used)++] = L' ';
            command_line[(*used)++] = L'"';
            for(const wchar_t *p = wide; *p != 0; p += 1)
            {
                if(*p == L'\\')
                {
                    size_t slashes = 0;
                    while(p[slashes] == L'\\') slashes += 1;
                    size_t count = slashes * ((p[slashes] == L'"' || p[slashes] == 0) ? 2u : 1u);
                    for(size_t i = 0; i < count; i += 1) command_line[(*used)++] = L'\\';
                    p += slashes - 1;
                }
                else
                {
                    if(*p == L'"') command_line[(*used)++] = L'\\';
                    command_line[(*used)++] = *p;
                }
            }
            command_line[(*used)++] = L'"';
            command_line[*used] = 0;
            ok = 1;
        }
    }
    return ok;
}

static int
write_config_string(FILE *file, const char *string)
{
    int ok = fputc('"', file) != EOF;
    for(const unsigned char *p = (const unsigned char *)string; *p != 0 && ok; p += 1)
    {
        if(*p < 32) ok = 0;
        else
        {
            if(*p == '\\' || *p == '"') ok = fputc('\\', file) != EOF;
            if(ok) ok = fputc(*p, file) != EOF;
        }
    }
    if(ok) ok = fputc('"', file) != EOF;
    return ok;
}

static int
write_project(Session *session)
{
    int ok = 0;
    FILE *project = fopen(session->project_path, "wb");
    if(project != NULL)
    {
        ok = fputs("// raddbg 0.9.27 project\ntarget:\n{\n enabled: 1\n executable: ", project) >= 0 &&
             write_config_string(project, session->args.debuggee) &&
             fputs("\n stdout_path: ", project) >= 0 && write_config_string(project, session->output_paths[0]) &&
             fputs("\n stderr_path: ", project) >= 0 && write_config_string(project, session->output_paths[1]) &&
             fputs("\n}\n", project) >= 0;
        if(fclose(project) != 0) ok = 0;
    }
    return ok;
}

static int
prepare_session(Session *session)
{
    int ok = 0;
    WSADATA startup;
    memset(&startup, 0, sizeof(startup));
    if(WSAStartup(MAKEWORD(2, 2), &startup) == 0) session->winsock_initialized = 1;
    DWORD occupied_pid = 0;
    int listener = session->winsock_initialized ? tcp_listener_owner(session->args.port, &occupied_pid) : -1;
    if(listener < 0)
    {
        log_text("RADDBG_ORACLE_ERROR cannot initialize Winsock or inspect Windows TCP listeners", NULL);
    }
    else if(listener != 0)
    {
        log_text("RADDBG_ORACLE_ERROR requested IPC port is already occupied", NULL);
    }
    else if(_mkdir(session->args.session_dir) != 0)
    {
        log_text("RADDBG_ORACLE_ERROR session directory must be new and creatable", NULL);
    }
    else
    {
        int paths_ok = format_path(session->temp_dir, sizeof(session->temp_dir), "%s", session->args.session_dir) &&
                       format_path(session->user_path, sizeof(session->user_path), "%s/session.raddbg_user", session->temp_dir) &&
                       format_path(session->project_path, sizeof(session->project_path), "%s/session.raddbg_project", session->temp_dir) &&
                       format_path(session->logs_path, sizeof(session->logs_path), "%s/logs", session->temp_dir) &&
                       format_path(session->output_paths[0], sizeof(session->output_paths[0]), "%s/target.stdout", session->temp_dir) &&
                       format_path(session->output_paths[1], sizeof(session->output_paths[1]), "%s/target.stderr", session->temp_dir);
        snprintf(session->port_arg, sizeof(session->port_arg), "--ipc_port:%u", session->args.port);
        if(paths_ok && _mkdir(session->logs_path) == 0 && write_project(session))
        {
            if(paths_ok)
            {
                session->job = CreateJobObjectW(NULL, NULL);
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
                memset(&limits, 0, sizeof(limits));
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                ok = session->job != NULL && SetInformationJobObject(session->job, JobObjectExtendedLimitInformation,
                                                                     &limits, sizeof(limits));
            }
        }
    }
    if(ok)
    {
        char user_arg[PATH_MAX + 32];
        char project_arg[PATH_MAX + 32];
        char logs_arg[PATH_MAX + 32];
        wchar_t application[PATH_MAX];
        wchar_t command_line[32768];
        size_t used = 0;
        command_line[0] = 0;
        ok = format_path(user_arg, sizeof(user_arg), "--user:%s", session->user_path) &&
             format_path(project_arg, sizeof(project_arg), "--project:%s", session->project_path) &&
             format_path(logs_arg, sizeof(logs_arg), "--logs:%s", session->logs_path) &&
             utf8_to_wide(session->args.raddbg, application, PATH_MAX);
        const char *arguments[] = {session->args.raddbg, user_arg, project_arg, logs_arg, session->port_arg};
        for(unsigned i = 0; i < sizeof(arguments) / sizeof(arguments[0]) && ok; i += 1)
            ok = append_windows_arg(command_line, sizeof(command_line) / sizeof(command_line[0]), &used, arguments[i]);
        if(ok)
        {
            STARTUPINFOW startup_info;
            PROCESS_INFORMATION process_info;
            memset(&startup_info, 0, sizeof(startup_info));
            memset(&process_info, 0, sizeof(process_info));
            startup_info.cb = sizeof(startup_info);
            /* Suspend before assigning the kill-on-close job so every child is
             * contained from its first instruction; require assignment success. */
            ok = CreateProcessW(application, command_line, NULL, NULL, FALSE, CREATE_SUSPENDED,
                                NULL, NULL, &startup_info, &process_info) != 0;
            if(ok)
            {
                session->gui_process = process_info.hProcess;
                session->gui_pid = process_info.dwProcessId;
                ok = AssignProcessToJobObject(session->job, process_info.hProcess) != 0 &&
                     ResumeThread(process_info.hThread) != (DWORD)-1;
                if(!ok) TerminateProcess(process_info.hProcess, 1);
                CloseHandle(process_info.hThread);
            }
        }
    }
    if(ok)
    {
        fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_SESSION_DIR %s\n", session->temp_dir);
        log_text("RADDBG_ORACLE_CONTROLLER Windows native loopback TCP; GUI owns listener and visible window", NULL);
        ok = wait_for_gui_window(session) && wait_for_owned_ipc(session);
    }
    return ok;
}

#else
/* Unix absolute paths begin with '/', which the pinned GUI parses as a CLI flag.
 * Store the controlled target in project data instead of a positional argument. */
static int
write_config_string(FILE *file, const char *string)
{
    int ok = fputc('"', file) != EOF;
    for(const unsigned char *p = (const unsigned char *)string; *p != 0 && ok; p += 1)
    {
        if(*p < 32) ok = 0;
        else
        {
            if(*p == '\\' || *p == '"') ok = fputc('\\', file) != EOF;
            if(ok) ok = fputc(*p, file) != EOF;
        }
    }
    if(ok) ok = fputc('"', file) != EOF;
    return ok;
}

static int
write_project(Session *session)
{
    int ok = 0;
    FILE *project = fopen(session->project_path, "wb");
    if(project != NULL)
    {
        ok = fputs("// raddbg 0.9.27 project\ntarget:\n{\n enabled: 1\n executable: ", project) >= 0 &&
             write_config_string(project, session->args.debuggee) &&
             fputs("\n}\n", project) >= 0;
        if(fclose(project) != 0) ok = 0;
    }
    return ok;
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
            if(paths_ok && mkdir(session->logs_path, 0700) == 0 && write_project(session) && pipe(output_pipes) == 0)
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

#endif

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

#if defined(_WIN32)
static int
target_process_alive(Session *session)
{
    int alive = session->target_process == NULL ||
                WaitForSingleObject(session->target_process, 0) != WAIT_OBJECT_0;
    return alive;
}
#else
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

#endif

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
#if defined(_WIN32)
            int alive = target_process_alive(session);
#else
            int alive = target_pid_alive(session->target_pid);
#endif
            if(marker_count == 1 && !alive)
            {
#if defined(_WIN32)
                DWORD exit_code = 0xffffffffu;
                ok = GetExitCodeProcess(session->target_process, &exit_code) && exit_code == 0;
                fprintf(g_log != NULL ? g_log : stdout, "RADDBG_ORACLE_TARGET_EXIT code=%lu\n", (unsigned long)exit_code);
#else
                ok = 1;
#endif
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

#if defined(_WIN32)
static int
cleanup_session(Session *session)
{
    int ok = 1;
    session->deadline_ms = monotonic_ms() + 10000u;
    if(session->gui_process != NULL)
    {
        Buffer response = {0};
        if(gui_alive(session) && process_owns_ipc_listener(session) &&
           run_ipc(session, "kill_all", &response) && response_equals(&response, "done"))
        {
            log_text("RADDBG_ORACLE_CLEANUP kill_all=done", NULL);
        }
        else
        {
            log_text("RADDBG_ORACLE_CLEANUP kill_all unavailable; terminating owned job", NULL);
            ok = 0;
        }
        free(response.data);
        if(session->job != NULL && !TerminateJobObject(session->job, 1))
        {
            DWORD error = GetLastError();
            log_windows_api_failure("TerminateJobObject", 0, error);
            ok = 0;
        }
        /* TerminateJobObject is asynchronous. Briefly wait before the
         * per-process fallback; keep one 5-second GUI-reap budget. */
        uint64_t gui_reap_deadline = monotonic_ms() + 5000u;
        DWORD gui_wait = WaitForSingleObject(session->gui_process, 100u);
        if(gui_wait == WAIT_TIMEOUT)
        {
            int terminated = TerminateProcess(session->gui_process, 1) != 0;
            DWORD terminate_error = terminated ? 0 : GetLastError();
            if(!terminated)
            {
                log_windows_api_failure("TerminateProcess", 0, terminate_error);
                if(terminate_error != ERROR_ACCESS_DENIED) ok = 0;
            }
            uint64_t now = monotonic_ms();
            DWORD remaining = now < gui_reap_deadline ? (DWORD)(gui_reap_deadline - now) : 0;
            gui_wait = WaitForSingleObject(session->gui_process, remaining);
        }
        if(gui_wait != WAIT_OBJECT_0)
        {
            DWORD wait_error = gui_wait == WAIT_FAILED ? GetLastError() : 0;
            log_windows_api_failure("WaitForSingleObject GUI", gui_wait, wait_error);
            ok = 0;
        }
        session->gui_reaped = gui_wait == WAIT_OBJECT_0;
        if(!CloseHandle(session->gui_process))
        {
            DWORD error = GetLastError();
            log_windows_api_failure("CloseHandle GUI", 0, error);
            ok = 0;
        }
        session->gui_process = NULL;
        session->gui_pid = 0;
    }
    if(session->job != NULL)
    {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
        int job_empty = 0;
        int job_query_ok = 1;
        uint64_t end = monotonic_ms() + 2000u;
        while(monotonic_ms() < end)
        {
            memset(&accounting, 0, sizeof(accounting));
            if(!QueryInformationJobObject(session->job, JobObjectBasicAccountingInformation,
                                           &accounting, sizeof(accounting), NULL))
            {
                DWORD error = GetLastError();
                log_windows_api_failure("QueryInformationJobObject", 0, error);
                job_query_ok = 0;
                break;
            }
            if(accounting.ActiveProcesses == 0)
            {
                job_empty = 1;
                break;
            }
            Sleep(20);
        }
        if(!job_empty)
        {
            if(job_query_ok)
            {
                log_windows_api_failure("job still has live descendants",
                                            accounting.ActiveProcesses, 0);
            }
            else
            {
                log_text("RADDBG_ORACLE_ERROR could not verify owned job is empty after cleanup", NULL);
            }
            ok = 0;
        }
        if(!CloseHandle(session->job))
        {
            DWORD error = GetLastError();
            log_windows_api_failure("CloseHandle job", 0, error);
            ok = 0;
        }
        session->job = NULL;
    }
    if(session->target_process != NULL)
    {
        DWORD target_wait = WaitForSingleObject(session->target_process, 2000);
        if(target_wait != WAIT_OBJECT_0)
        {
            DWORD error = target_wait == WAIT_FAILED ? GetLastError() : 0;
            log_windows_api_failure("WaitForSingleObject debuggee", target_wait, error);
            ok = 0;
        }
        if(!CloseHandle(session->target_process))
        {
            DWORD error = GetLastError();
            log_windows_api_failure("CloseHandle debuggee", 0, error);
            ok = 0;
        }
        session->target_process = NULL;
    }
    if(!drain_gui_output(session)) ok = 0;
    for(unsigned i = 0; i < 2; i += 1)
    {
        if(session->output_files[i] != NULL)
        {
            if(!CloseHandle(session->output_files[i]))
            {
                DWORD error = GetLastError();
                log_windows_api_failure("CloseHandle output", 0, error);
                ok = 0;
            }
            session->output_files[i] = NULL;
        }
    }
    /* Keep the connection alive until the owned GUI and descendants are dead.
     * The pinned listener requeues zero-byte TCP receives rather than removing
     * closed connections, so short-lived clients could flood its receive ring. */
    if(session->ipc_socket != INVALID_SOCKET)
    {
        if(closesocket(session->ipc_socket) != 0)
        {
            int error = WSAGetLastError();
            log_windows_api_failure("closesocket IPC", (unsigned long)SOCKET_ERROR,
                                        (unsigned long)error);
            ok = 0;
        }
        session->ipc_socket = INVALID_SOCKET;
    }
    if(session->winsock_initialized)
    {
        if(WSACleanup() != 0)
        {
            int error = WSAGetLastError();
            log_windows_api_failure("WSACleanup", (unsigned long)SOCKET_ERROR,
                                        (unsigned long)error);
            ok = 0;
        }
        session->winsock_initialized = 0;
    }
    free(session->gui_output.data);
    session->gui_output.data = NULL;
    session->gui_output.size = 0;
    if(ok) log_text("RADDBG_ORACLE_CLEANUP status=pass", NULL);
    return ok;
}

#else
static int
cleanup_session(Session *session)
{
    int ok = 1;
    uint64_t cleanup_deadline = monotonic_ms() + 10000u;
    session->deadline_ms = cleanup_deadline;
    if(session->gui_pid > 0)
    {
        Buffer response = {0};
        /* Reserve half of cleanup's budget for termination and reaping. */
        session->deadline_ms = monotonic_ms() + 5000u;
        if(!session->gui_reaped && process_owns_ipc_listener(session) &&
           run_ipc(session, "kill_all", &response) && response_equals(&response, "done"))
        {
            log_text("RADDBG_ORACLE_CLEANUP kill_all=done", NULL);
        }
        else
        {
            log_text("RADDBG_ORACLE_CLEANUP kill_all=unavailable; terminating the owned tracer", NULL);
            ok = 0;
        }
        free(response.data);
        session->deadline_ms = cleanup_deadline;
        kill(-session->gui_group, SIGTERM);
        uint64_t end = monotonic_ms() + 1000u;
        if(end > cleanup_deadline) end = cleanup_deadline;
        int status = 0;
        int reaped = session->gui_reaped;
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
            uint64_t now = monotonic_ms();
            uint64_t remaining = now < cleanup_deadline ? cleanup_deadline - now : 0;
            uint32_t reap_timeout = remaining > 3000u ? 3000u : (uint32_t)remaining;
            if(reap_child_bounded(session->gui_pid, &status, reap_timeout)) reaped = 1;
            else ok = 0;
        }
        kill(-session->gui_group, SIGKILL);
        session->gui_reaped = reaped;
        if(reaped) session->gui_pid = 0;
        if(!reaped)
        {
            log_text("RADDBG_ORACLE_ERROR could not reap RAD GUI process", NULL);
        }
    }
    if(session->target_pid > 0)
    {
        uint64_t end = monotonic_ms() + 2000u;
        if(end > cleanup_deadline) end = cleanup_deadline;
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
    if(!linux_close_ipc_socket_after_gui(session)) ok = 0;
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

#endif

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
        " ip_module: \"fixture\"\n ip_voff: 0x10\n ip_voff_symbol: \"debuggee_inner\"\n"
        " stop_event:\n {\n }\n locals:\n {\n  seed\n }\n lines:\n {\n  {\n   file_name: \"fixture.c\"\n   line_num: 31\n   voff_range: [0x10, 0x11)\n  }\n }\n"
        " threads:\n {\n  {\n   name: \"main\"\n   id: 1\n   ip: 0x10\n  }\n }\n modules:\n {\n }\n}\n";
    const char *prelaunch_state =
        "state:\n{\n running: 0\n run_gen: 14\n stop_count: 1\n ip: 0x0\n"
        " ip_module: \"\"\n ip_voff: 0x0\n ip_voff_symbol: \"\"\n"
        " stop_event:\n {\n  explanation: \"Not running\"\n }\n locals:\n {\n }\n lines:\n {\n }\n"
        " threads:\n {\n }\n modules:\n {\n }\n}\n";
    const char *invalid_prelaunch_state =
        "state:\n{\n running: 0\n run_gen: 14\n stop_count: 1\n ip: 0x0\n"
        " ip_module: \"\"\n ip_voff: 0x0\n ip_voff_symbol: \"\"\n"
        " stop_event:\n {\n  explanation: \"Breakpoint hit\"\n }\n locals:\n {\n }\n lines:\n {\n }\n"
        " threads:\n {\n }\n modules:\n {\n }\n}\n";
    /* Captured from the pinned trusted Windows GUI on hosted Actions. */
    const char *windows_state =
        "state:\n"
        "{\n"
        " running: 0\n"
        " run_gen: 7\n"
        " stop_count: 1\n"
        " ip: 0x7ff6518311a0\n"
        " ip_module: \"raddebugger-debuggee.exe\"\n"
        " ip_voff: 0x11a0\n"
        " ip_voff_symbol: \"debuggee_outer\"\n"
        " stop_event:\n"
        " {\n"
        "  arch: Null\n"
        "  vaddr_range: [0x0, 0x0)\n"
        "  ip_vaddr: 0x7ff6518311a0\n"
        "  stack_base: 0x0\n"
        "  tls_root: 0x0\n"
        "  tls_index: 0x0\n"
        "  timestamp: 0x0\n"
        "  exception_code: 0x80000003\n"
        "  bp_flags: 0x0\n"
        "  string: \"\"\n"
        "  explanation: \"main_thread hit a breakpoint\"\n"
        " }\n"
        " locals:\n"
        " {\n"
        " seed\n"
        " values\n"
        " record\n"
        " outer_value\n"
        " outer_result\n"
        " final_result\n"
        " }\n"
        " lines:\n"
        " {\n"
        "  {\n"
        "   file_name:  \"raddebugger_debuggee.c\"\n"
        "   line_num:   44\n"
        "   column_num: 1\n"
        "   voff_range: [0x11a0, 0x11a8)\n"
        "  }\n"
        " }\n"
        " threads:\n"
        " {\n"
        "  {\n"
        "   name: \"main_thread\"\n"
        "   id:   5000\n"
        "   ip:   0x7ff6518311a0\n"
        "  }\n"
        " }\n"
        " modules:\n"
        " {\n"
        "  {\n"
        "   name:        \"D:/a/buster/buster/build/raddebugger-windows-892-435644236/clang/raddebugger-debuggee.exe\"\n"
        "   vaddr_range: [0x7ff651830000, 0x7ff65185c000)\n"
        "  }\n"
        "  {\n"
        "   name:        \"C:/Windows/System32/ntdll.dll\"\n"
        "   vaddr_range: [0x7ffb23fa0000, 0x7ffb24204000)\n"
        "  }\n"
        "  {\n"
        "   name:        \"C:/Windows/System32/kernel32.dll\"\n"
        "   vaddr_range: [0x7ffb22880000, 0x7ffb22949000)\n"
        "  }\n"
        "  {\n"
        "   name:        \"C:/Windows/System32/KernelBase.dll\"\n"
        "   vaddr_range: [0x7ffb215e0000, 0x7ffb219ce000)\n"
        "  }\n"
        " }\n"
        "}\n";
    const char *duplicate_state =
        "state:\n{\n running: 0\n run_gen: 3\n stop_count: 4\n stop_count: 5\n ip: 0x10\n"
        " ip_module: \"fixture\"\n ip_voff: 0x10\n ip_voff_symbol: \"debuggee_inner\"\n"
        " stop_event:\n {\n }\n locals:\n {\n  seed\n }\n lines:\n {\n  {\n   file_name: \"fixture.c\"\n   line_num: 31\n   voff_range: [0x10, 0x11)\n  }\n }\n"
        " threads:\n {\n  {\n   name: \"main\"\n   id: 1\n   ip: 0x10\n  }\n }\n modules:\n {\n }\n}\n";
    const char *missing_state =
        "state:\n{\n running: 0\n run_gen: 3\n stop_count: 4\n ip: 0x10\n"
        " ip_module: \"fixture\"\n ip_voff_symbol: \"debuggee_inner\"\n"
        " stop_event:\n {\n }\n locals:\n {\n  seed\n }\n lines:\n {\n  {\n   file_name: \"fixture.c\"\n   line_num: 31\n   voff_range: [0x10, 0x11)\n  }\n }\n"
        " threads:\n {\n  {\n   name: \"main\"\n   id: 1\n   ip: 0x10\n  }\n }\n modules:\n {\n }\n}\n";
    const char *duplicate_line_records =
        " lines:\n {\n"
        "  {\n   file_name: \"fixture.c\"\n   line_num: 31\n   voff_range: [0x10, 0x11)\n  }\n"
        "  {\n   file_name: \"fixture.c\"\n   line_num: 31\n   voff_range: [0x10, 0x11)\n  }\n"
        " }\n threads:\n";
    const char *zero_line_records =
        " lines:\n {\n  {\n   file_name: \"fixture.c\"\n   line_num: 0\n   voff_range: [0x10, 0x11)\n  }\n }\n threads:\n";
    const char *truncated_state = "state:\n{\n running: 0\n stop_count: 4\n";
    const char *valid_eval = "eval:\n{\n expr: inner_value\n value: \"17\"\n type: \"int\"\n msgs: \"\"\n}\n";
    const char *duplicate_eval = "eval:\n{\n expr: inner_value\n expr: wrong\n value: \"17\"\n type: \"int\"\n msgs: \"\"\n}\n";
    const char *truncated_eval = "eval:\n{\n expr: inner_value\n value: \"17\"\n";
    State state = {0};
    State prelaunch = {0};
    EvalResult eval = {0};
    EvalResult bad_eval = {0};
    unsigned matched_source_line = 0;
    Buffer cap = {0};
    char *big = malloc(MAX_IPC_BYTES + 1u);
    if(!parse_state(valid_state, &state) || state.stop_count != 4 || state.running ||
       state.thread_count != 1 || state.first_thread_id != 1 || !line_matches(valid_state, "fixture.c", 31, 0x10) ||
       !line_matches(valid_state, "fixture.c", 0, 0x10) ||
       !source_line_at_ip(valid_state, "fixture.c", 0x10, &matched_source_line) || matched_source_line != 31 ||
       !selected_thread_id(&state, &state.first_thread_id) ||
       parse_state(duplicate_state, &state) || parse_state(missing_state, &state) || parse_state(truncated_state, &state) ||
       !parse_state(prelaunch_state, &prelaunch) || !prelaunch.prelaunch || prelaunch.ip != 0 ||
       parse_state(invalid_prelaunch_state, &state) ||
       !parse_eval(valid_eval, &eval) || !eval_matches(&eval, "inner_value", "17") ||
       line_matches(valid_state, "fixture.c", 32, 0x10) || line_matches(valid_state, "fixture.c", 31, 0x11) ||
       line_matches(valid_state, "fixture.c", 0, 0x11) || line_matches(valid_state, "wrong.c", 0, 0x10) ||
       line_matches(duplicate_line_records, "fixture.c", 0, 0x10) ||
       line_matches(zero_line_records, "fixture.c", 0, 0x10) ||
       parse_eval(duplicate_eval, &bad_eval) || parse_eval(truncated_eval, &bad_eval))
    {
        ok = 0;
    }
    const char *valid_locals = " locals:\n {\n seed\n }\n";
    const char *duplicate_locals = " locals:\n {\n seed\n seed\n }\n";
    const char *empty_locals = " locals:\n {\n }\n";
    if(!has_local(valid_locals, "seed") || has_local(valid_locals, "outer_value") ||
       !has_local(windows_state, "outer_value") || !has_local(windows_state, "record") ||
       has_local(windows_state, "worker_seed") || has_local(duplicate_locals, "seed") ||
       has_local(empty_locals, "seed") || has_local("state:\n{\n}\n", "seed"))
    {
        ok = 0;
    }
    State windows_parsed = {0};
    uint64_t windows_selected_id = 0;
    if(!module_matches("fixture", "/tmp/fixture") || module_matches("wrong_fixture", "/tmp/fixture") ||
       module_matches("", "/tmp/fixture") || module_matches("fixture", ""))
    {
        ok = 0;
    }
    if(!parse_state(windows_state, &windows_parsed) || windows_parsed.running ||
       windows_parsed.run_gen != 7 || windows_parsed.stop_count != 1 ||
       windows_parsed.ip != 0x7ff6518311a0ull || windows_parsed.ip_voff != 0x11a0 ||
       windows_parsed.thread_count != 1 || windows_parsed.first_thread_id != 5000 ||
       !module_matches(windows_parsed.module, "C:\\fixture\\raddebugger-debuggee.exe") ||
       module_matches("wrong_module.exe", "C:\\fixture\\raddebugger-debuggee.exe") ||
       !symbol_matches(windows_parsed.symbol, "debuggee_outer") ||
       !selected_thread_id(&windows_parsed, &windows_selected_id) || windows_selected_id != 5000 ||
       !line_matches(windows_state, "C:\\fixture\\raddebugger_debuggee.c", 44, 0x11a0) ||
       line_matches(windows_state, "raddebugger_debuggee.c", 44, 0x11a8))
    {
        ok = 0;
    }
#if defined(_WIN32)
    if(!module_matches(windows_parsed.module, "C:\\fixture\\RADDEBUGGER-DEBUGGEE.EXE")) ok = 0;
#endif
    Buffer actual_response = {(char *)windows_state, strlen(windows_state), 0};
    Buffer truncated_response = {(char *)truncated_state, strlen(truncated_state), 0};
    Buffer done_response = {(char *)"done", 4, 0};
    Buffer partial_done_response = {(char *)"don", 3, 0};
    Buffer wrong_response = {(char *)"other:\n{}\n", strlen("other:\n{}\n"), 0};
    if(!native_response_complete(&actual_response) || native_response_complete(&truncated_response) ||
       !native_response_complete(&done_response) || native_response_complete(&partial_done_response) ||
       native_response_complete(&wrong_response))
    {
        ok = 0;
    }
    /* Actual pinned Eval wire layout, received in two independent TCP chunks. */
    static const char segmented_eval[] =
        "eval:\n{\n\0"
        " expr:  inner_value\n\0"
        " value: \"17\"\n\0"
        " type:  \"int\"\n\0"
        " msgs:  \"\"\n\0"
        "}\n";
    static const char misplaced_separator[] =
        "eval:\n{\n \0"
        "expr:  inner_value\n\0"
        " value: \"17\"\n\0"
        " type:  \"int\"\n\0"
        " msgs:  \"\"\n\0"
        "}\n";
    static const char interior_separator[] =
        "eval:\n{\n\0"
        " expr:  inner_value\n\0"
        " value: \"1\0" "7\"\n\0"
        " type:  \"int\"\n\0"
        " msgs:  \"\"\n\0"
        "}\n";
    static const char missing_separator[] =
        "eval:\n{\n\0"
        " expr:  inner_value\n"
        " value: \"17\"\n\0"
        " type:  \"int\"\n\0"
        " msgs:  \"\"\n\0"
        "}\n";
    static const char extra_separator[] =
        "eval:\n{\n\0"
        " expr:  inner_value\n\0"
        " value: \"17\"\n\0"
        " type:  \"int\"\n\0"
        " msgs:  \"\"\n\0"
        "}\n\0";
    static const char state_separator[] = "state:\n{\n\0}\n";
    static const char second_root[] =
        "eval:\n{\n expr: inner_value\n value: \"17\"\n type: \"int\"\n msgs: \"\"\n}\n{}\n";
    static const char done_suffix[] =
        "eval:\n{\n expr: inner_value\n value: \"17\"\n type: \"int\"\n msgs: \"\"\n}\ndone\n";
    static const char unexpected_field[] =
        "eval:\n{\n expr: inner_value\n value: \"17\"\n type: \"int\"\n msgs: \"\"\n other: \"x\"\n}\n";
    Buffer wire = {0};
    Buffer normalized = {0};
    size_t first_chunk = sizeof(segmented_eval) / 2u;
    if(!buffer_append(&wire, segmented_eval, first_chunk) ||
       normalize_native_response(&wire, &normalized) || normalized.data != NULL ||
       !buffer_append(&wire, segmented_eval + first_chunk, sizeof(segmented_eval) - 1u - first_chunk) ||
       native_response_complete(&wire) || !normalize_native_response(&wire, &normalized) ||
       !parse_eval(normalized.data, &bad_eval) || !eval_matches(&bad_eval, "inner_value", "17"))
    {
        ok = 0;
    }
    free(wire.data);
    free(normalized.data);
    const char *malformed_wire[] = {misplaced_separator, interior_separator, missing_separator,
                                   extra_separator, state_separator, second_root, done_suffix, unexpected_field};
    const size_t malformed_sizes[] = {sizeof(misplaced_separator)-1u, sizeof(interior_separator)-1u,
                                     sizeof(missing_separator)-1u, sizeof(extra_separator)-1u,
                                     sizeof(state_separator)-1u, sizeof(second_root)-1u,
                                     sizeof(done_suffix)-1u, sizeof(unexpected_field)-1u};
    for(unsigned i = 0; i < sizeof(malformed_sizes)/sizeof(malformed_sizes[0]); i += 1)
    {
        Buffer raw = {(char *)malformed_wire[i], malformed_sizes[i], 0};
        Buffer rejected = {0};
        if(normalize_native_response(&raw, &rejected) || rejected.data != NULL || rejected.size != 0) ok = 0;
        free(rejected.data);
    }
    if(parse_eval(second_root, &bad_eval) || parse_eval(done_suffix, &bad_eval) ||
       parse_eval(unexpected_field, &bad_eval) || balanced_md("state:\n{}\n{}\n") ||
       balanced_md("state:\n{}\ndone\n"))
    {
        ok = 0;
    }
    static const char completion_line[] = "RADDEBUGGER_DEBUGGEE completion\n";
    static const char hidden_completion[] = "\0RADDEBUGGER_DEBUGGEE completion\n";
    Session captured = {0};
    if(!append_captured_output(&captured.gui_output, completion_line, sizeof(completion_line)-1u) ||
       append_captured_output(&captured.gui_output, hidden_completion, sizeof(hidden_completion)-1u) ||
       captured.gui_output.size != sizeof(completion_line)-1u ||
       gui_output_line_count(&captured, "RADDEBUGGER_DEBUGGEE completion") != 1)
    {
        ok = 0;
    }
    free(captured.gui_output.data);
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
#if defined(_WIN32)
    session.ipc_socket = INVALID_SOCKET;
#else
    session.gui_output_fd = -1;
    session.ipc_socket = -1;
#endif
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
#if defined(_WIN32)
        log_windows_launch_context(&session, command_text);
#endif
        if(!send_and_wait(&session, command_text, &initial, "debuggee_outer", 0, &next))
        {
#if defined(_WIN32)
            log_windows_failure_context(&session);
#endif
            sequence_ok = 0;
        }
        if(sequence_ok) state = next;
        if(sequence_ok && (state.thread_count != 1 || state.first_thread_id == 0)) sequence_ok = 0;

        snprintf(command_text, sizeof(command_text), "run_to_line %s:%u", args->source, lines.outer);
        if(sequence_ok && !send_and_wait(&session, command_text, &state, "debuggee_outer", lines.outer, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            if(state.thread_count != 1 || !expect_values(&session, &state, outer_values, sizeof(outer_values) / sizeof(outer_values[0]))) sequence_ok = 0;
        }

        uint64_t main_inner_thread_id = 0;
        if(sequence_ok && !selected_thread_id(&state, &main_inner_thread_id)) sequence_ok = 0;
        if(sequence_ok && !send_and_wait(&session, "step_into", &state, "debuggee_inner", 0, &next)) sequence_ok = 0;
        if(sequence_ok && !selected_thread_is(&next, main_inner_thread_id)) sequence_ok = 0;
        if(sequence_ok) state = next;
        if(sequence_ok && !step_over_to_inner_marker(&session, &state, lines.inner, main_inner_thread_id)) sequence_ok = 0;
        if(sequence_ok && !expect_values(&session, &state, inner_values, sizeof(inner_values) / sizeof(inner_values[0]))) sequence_ok = 0;
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
            if(state.thread_count != 2 || !selected_thread_id(&state, &worker_id) || worker_id == session.main_thread_id ||
               !state_has_thread_id(&state, session.main_thread_id) ||
               !expect_values(&session, &state, worker_values, sizeof(worker_values) / sizeof(worker_values[0]))) sequence_ok = 0;
        }

        snprintf(command_text, sizeof(command_text), "run_to_line %s:%u", args->source, lines.outer);
        if(sequence_ok && !send_and_wait(&session, command_text, &state, "debuggee_outer", lines.outer, &next)) sequence_ok = 0;
        if(sequence_ok)
        {
            state = next;
            if(!expect_values(&session, &state, worker_outer_values, sizeof(worker_outer_values) / sizeof(worker_outer_values[0]))) sequence_ok = 0;
        }

        uint64_t worker_inner_thread_id = 0;
        if(sequence_ok && !selected_thread_id(&state, &worker_inner_thread_id)) sequence_ok = 0;
        if(sequence_ok && !send_and_wait(&session, "step_into", &state, "debuggee_inner", 0, &next)) sequence_ok = 0;
        if(sequence_ok && !selected_thread_is(&next, worker_inner_thread_id)) sequence_ok = 0;
        if(sequence_ok) state = next;
        if(sequence_ok && !step_over_to_inner_marker(&session, &state, lines.inner, worker_inner_thread_id)) sequence_ok = 0;
        if(sequence_ok && !expect_values(&session, &state, worker_inner_values, sizeof(worker_inner_values) / sizeof(worker_inner_values[0]))) sequence_ok = 0;
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
                if(eval_once(&session, "outer_value", "10", state.stop_count, NULL))
                {
                    log_text("RADDBG_ORACLE_UNWIND outer_value=10", NULL);
                    if(command(&session, "up_one_frame", &response) && eval_once(&session, "inner_value", "21", state.stop_count, NULL))
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
#if defined(_WIN32)
    if(_setmode(_fileno(stdout), _O_BINARY) == -1)
    {
        fprintf(stderr, "RADDBG_ORACLE_ERROR cannot set binary stdout\n");
    }
    else
    {
        g_log = stdout;
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#else
    struct sigaction action = {0};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
#endif
#if !defined(_WIN32)
    g_log = stdout;
#endif
#if defined(_WIN32)
    int output_ready = g_log != NULL;
#else
    int output_ready = 1;
#endif
    if(output_ready && parse_args(argc, argv, &args))
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
