/* Hosted diagnostic for #1931. Reuses tp_process without changing it.
 * A readiness pipe proves the helper is alive before its principal exits.
 * Root withholds helper release until tp_process has returned; this is a
 * process-boundary witness, never compiler timing or bundle acceptance.
 */
#define main throughput_cli_main
#include "../../throughput/throughput.c"
#undef main

static void probe_delay(unsigned milliseconds)
{
    struct timespec remaining = {(time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000L};
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) { }
}

static int probe_principal(char const* mode, char const* marker, int release_read, int release_write)
{
    int result = 3;
    int ready[2] = {-1, -1};
    if (!strcmp(mode, "failure")) result = 7;
    else if (pipe(ready) == 0)
    {
        pid_t helper = fork();
        if (helper == 0)
        {
            close(ready[0]);
            close(release_write);
            char byte = 1;
            int ok = write(ready[1], &byte, 1) == 1;
            close(ready[1]);
            ssize_t received;
            do { received = read(release_read, &byte, 1); } while (received < 0 && errno == EINTR);
            close(release_read);
            ok = ok && received == 1;
            if (ok)
            {
                probe_delay(50);
                ok = file_write(string_from_pointer(marker), (ByteSlice){(u8*)"helper-complete", 15});
            }
            _exit(ok ? 0 : 3);
        }
        close(ready[1]);
        char byte;
        ssize_t received;
        do { received = read(ready[0], &byte, 1); } while (received < 0 && errno == EINTR);
        close(ready[0]);
        int status = 0;
        int live = helper > 0 && received == 1 && waitpid(helper, &status, WNOHANG) == 0;
        if (live)
        {
            printf("HELPER_READY mode=%s helper=%ld alive_before_exit=1\n", mode, (long)helper);
            if (fflush(stdout) != 0) live = 0;
        }
        if (live && !strcmp(mode, "orphan"))
        {
            /* The helper cannot finish: the root still holds its release. */
            result = 0;
        }
        else if (live && !strcmp(mode, "wait"))
        {
            byte = 1;
            int released = write(release_write, &byte, 1) == 1;
            pid_t waited;
            do { waited = waitpid(helper, &status, 0); } while (waited < 0 && errno == EINTR);
            if (released && waited == helper && WIFEXITED(status) && WEXITSTATUS(status) == 0) result = 0;
        }
    }
    close(release_read);
    close(release_write);
    return result;
}

int main(int argc, char** argv)
{
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    int result = 2;
    if (argc == 6)
    {
        result = probe_principal(argv[1], argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    else if (argc == 2)
    {
        char self[TP_PATH_CAP], marker[TP_PATH_CAP], log[TP_PATH_CAP];
        int ok = tp_absolute(argv[0], self);
        char const* modes[] = {"wait", "orphan", "failure"};
        for (unsigned scenario = 0; scenario < 3 && ok; ++scenario)
        {
            char marker_name[128], log_name[128];
            snprintf(marker_name, sizeof(marker_name), "%s.marker", modes[scenario]);
            snprintf(log_name, sizeof(log_name), "%s.log", modes[scenario]);
            ok = tp_path(marker, argv[1], marker_name) && tp_path(log, argv[1], log_name);
            int release[2] = {-1, -1};
            ok = ok && pipe(release) == 0;
            if (ok)
            {
                char read_text[32], write_text[32];
                snprintf(read_text, sizeof(read_text), "%d", release[0]);
                snprintf(write_text, sizeof(write_text), "%d", release[1]);
                char* arguments[] = {self, (char*)modes[scenario], marker, read_text, write_text, "child", NULL};
                TpProcess process = tp_process(arguments, NULL, log, 3, -1, 0);
                /* Keep root's read end open until after release, avoiding
                 * SIGPIPE if the unchanged harness already killed the helper. */
                char byte = 1;
                int released = write(release[1], &byte, 1) == 1;
                close(release[0]);
                close(release[1]);
                probe_delay(200);
                struct stat info;
                int marker_present = stat(marker, &info) == 0;
                int marker_valid = marker_present ? info.st_size == 15 : errno == ENOENT;
                int status_expected = scenario == 2 ? 7 : 0;
                printf("CHILD_ACCOUNTING mode=%s exit=%d launch_error=%d timeout=%d signal=%d marker=%d released=%d\n",
                       modes[scenario], process.exit_code, process.launch_error, process.timed_out,
                       process.signal_number, marker_present, released);
                ok = released && process.exit_code == status_expected && !process.launch_error &&
                     !process.timed_out && !process.signal_number && marker_valid && marker_present == (scenario == 0);
            }
        }
        result = ok ? 0 : 1;
    }
    thread_context_release(context);
    arena_pool_release_thread();
    return result;
}
