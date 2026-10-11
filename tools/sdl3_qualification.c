// Opt-in Linux SDL3 intake. main dispatches the C compiler adapter or campaign.
// sdl_run owns bounded, lossless argv capture. sdl_pristine rejects dirty/wrong
// upstream inputs. CMake probes are never seeded or replaced.
// This first gate does not establish hardware or performance qualification.
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_SDL_PIN "7f3ae3d57459e59943a4ecfefc8f6277ec6bf540"
#define BUSTER_SDL_PATH 4096
#define BUSTER_SDL_ARGS 512
#define BUSTER_SDL_TEXT (4 * 1024 * 1024)

static char sdl_text[BUSTER_SDL_TEXT];
static char sdl_manifest[BUSTER_SDL_TEXT];
static size_t sdl_text_size;
extern char** environ;
static const char* sdl_out;
static unsigned sdl_sequence;

static void sdl_path(char out[BUSTER_SDL_PATH], const char* root, const char* name)
{
    int length = snprintf(out, BUSTER_SDL_PATH, "%s/%s", root, name);
    if (length < 0 || length >= BUSTER_SDL_PATH)
    {
        fprintf(stderr, "SDL path exceeds admitted bound\n");
        exit(2);
    }
}

static bool sdl_read(const char* path)
{
    FILE* file = fopen(path, "rb");
    bool ok = file != NULL;
    size_t count = 0;
    if (ok)
    {
        count = fread(sdl_text, 1, sizeof(sdl_text) - 1, file);
        ok = !ferror(file) && feof(file);
        ok = fclose(file) == 0 && ok;
    }
    sdl_text[count] = 0;
    sdl_text_size = count;
    return ok;
}

static bool sdl_argv(FILE* file, char* const args[])
{
    char directory[BUSTER_SDL_PATH];
    bool ok = getcwd(directory, sizeof(directory)) != NULL;
    if (ok)
    {
        ok = fprintf(file, "cwd-bytes=%zu\n%s\n", strlen(directory), directory) >= 0;
    }
    for (unsigned i = 0; ok && args[i]; i += 1)
    {
        ok = fprintf(file, "arg[%u]-bytes=%zu\n%s\n", i, strlen(args[i]), args[i]) >= 0;
    }
    return ok;
}

// GNU timeout is an external process supervisor; execvp keeps argv unchanged.
static int sdl_run(const char* role, char* const args[], const char* seconds)
{
    char name[128], prefix[BUSTER_SDL_PATH], path[BUSTER_SDL_PATH];
    int n = snprintf(name, sizeof(name), "%03u-%s", sdl_sequence++, role);
    if (n < 0 || n >= (int)sizeof(name))
    {
        exit(2);
    }
    sdl_path(prefix, sdl_out, name);
    n = snprintf(path, sizeof(path), "%s.argv", prefix);
    FILE* argv_file = n >= 0 && n < (int)sizeof(path) ? fopen(path, "wx") : NULL;
    bool ok = argv_file != NULL;
    if (ok)
    {
        ok = sdl_argv(argv_file, args);
        ok = fclose(argv_file) == 0 && ok;
    }
    char* supervised[BUSTER_SDL_ARGS];
    unsigned count = 0;
    supervised[count++] = "timeout";
    supervised[count++] = "--signal=TERM";
    supervised[count++] = "--kill-after=5";
    supervised[count++] = (char*)seconds;
    for (unsigned i = 0; args[i] && ok; i += 1)
    {
        ok = count + 1 < BUSTER_SDL_ARGS;
        if (ok)
        {
            supervised[count++] = args[i];
        }
    }
    supervised[count] = NULL;
    int status = 125;
    struct rusage usage = {0};
    pid_t child = ok ? fork() : -1;
    if (child == 0)
    {
        char log_path[BUSTER_SDL_PATH];
        n = snprintf(log_path, sizeof(log_path), "%s.log", prefix);
        if (n < 0 || n >= (int)sizeof(log_path))
        {
            _exit(125);
        }
        int fd = open(log_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
        {
            _exit(125);
        }
        close(fd);
        if (!strcmp(args[0], "git") &&
            (setenv("GIT_CONFIG_NOSYSTEM", "1", 1) != 0 ||
             setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1) != 0 ||
             setenv("GIT_ATTR_NOSYSTEM", "1", 1) != 0))
        {
            _exit(125);
        }
        execvp(supervised[0], supervised);
        _exit(127);
    }
    else if (child > 0)
    {
        int native_status = 0;
        pid_t waited;
        do
        {
            waited = wait4(child, &native_status, 0, &usage);
        } while (waited < 0 && errno == EINTR);
        if (waited == child && WIFEXITED(native_status))
        {
            status = WEXITSTATUS(native_status);
        }
        else if (waited == child && WIFSIGNALED(native_status))
        {
            status = 128 + WTERMSIG(native_status);
        }
    }
    n = snprintf(path, sizeof(path), "%s.status", prefix);
    FILE* report = n >= 0 && n < (int)sizeof(path) ? fopen(path, "wx") : NULL;
    if (report)
    {
        int written = fprintf(report, "status=%d\nmaxrss_kib=%ld\nmeasurement=hosted-child-tree-high-water-diagnostic\n", status, usage.ru_maxrss);
        if (fclose(report) != 0 || written < 0)
        {
            status = 125;
        }
    }
    else
    {
        status = 125;
    }
    printf("SDL_STAGE %s status=%d log=%s.log\n", role, status, prefix);
    if (strstr(role, "configure") || strstr(role, "build") || strstr(role, "tests") || strstr(role, "fixture"))
    {
        n = snprintf(path, sizeof(path), "%s.log", prefix);
        if (n >= 0 && n < (int)sizeof(path))
        {
            FILE* log = fopen(path, "rb");
            if (log)
            {
                char excerpt[8193];
                size_t bytes = fread(excerpt, 1, sizeof(excerpt) - 1, log);
                excerpt[bytes] = 0;
                printf("SDL_LOG_BEGIN %s\n%s\n", role, excerpt);
                if (!feof(log) && fseek(log, -8192, SEEK_END) == 0)
                {
                    bytes = fread(excerpt, 1, sizeof(excerpt) - 1, log);
                    excerpt[bytes] = 0;
                    printf("SDL_LOG_TAIL %s\n%s\n", role, excerpt);
                }
                fclose(log);
                printf("SDL_LOG_END %s\n", role);
            }
        }
    }
    return status;
}

// Git environment redirection is refused before these controlled subprocesses.
// Every output is retained; object comparisons bypass index flags and filters.
static int sdl_git(const char* role, const char* source, char* const parameters[])
{
    char* args[BUSTER_SDL_ARGS] = {"git", "--no-replace-objects",
        "-c", "core.fsmonitor=false", "-c", "core.untrackedCache=false",
        "-C", (char*)source, NULL};
    unsigned count = 8;
    bool ok = true;
    for (unsigned i = 0; parameters[i] && ok; i += 1)
    {
        ok = count + 1 < BUSTER_SDL_ARGS;
        if (ok)
        {
            args[count++] = parameters[i];
        }
    }
    args[count] = NULL;
    unsigned sequence = sdl_sequence;
    int result = ok ? sdl_run(role, args, "20") : 125;
    char name[128], path[BUSTER_SDL_PATH];
    snprintf(name, sizeof(name), "%03u-%s.log", sequence, role);
    sdl_path(path, sdl_out, name);
    if (!sdl_read(path))
    {
        result = 125;
    }
    return result;
}

static bool sdl_raw_objects(const char* source)
{
    char* tree[] = {"ls-tree", "-rz", "--full-tree", BUSTER_SDL_PIN, NULL};
    bool ok = sdl_git("source-tree", source, tree) == 0 && sdl_text_size > 0;
    size_t size = sdl_text_size, offset = 0;
    if (ok)
    {
        memcpy(sdl_manifest, sdl_text, size);
    }
    while (ok && offset < size)
    {
        char* args[132] = {"hash-object", "--no-filters", "--", NULL};
        char* entries[128];
        unsigned count = 0;
        while (ok && offset < size && count < 128)
        {
            char* entry = sdl_manifest + offset;
            char* end = memchr(entry, 0, size - offset);
            ok = end && end - entry > 53 &&
                (!memcmp(entry, "100644 blob ", 12) || !memcmp(entry, "100755 blob ", 12)) &&
                entry[52] == '\t';
            if (ok)
            {
                char path[BUSTER_SDL_PATH];
                sdl_path(path, source, entry + 53);
                struct stat file;
                ok = lstat(path, &file) == 0 && S_ISREG(file.st_mode) &&
                    ((file.st_mode & 0111) != 0) == (entry[3] == '7');
            }
            if (ok)
            {
                entries[count] = entry;
                args[3 + count++] = entry + 53;
                offset = (size_t)(end - sdl_manifest) + 1;
            }
        }
        args[3 + count] = NULL;
        if (ok)
        {
            ok = sdl_git("source-raw-blobs", source, args) == 0 &&
                sdl_text_size == (size_t)count * 41;
            for (unsigned i = 0; ok && i < count; i += 1)
            {
                ok = !memcmp(sdl_text + i * 41, entries[i] + 12, 40) &&
                    sdl_text[i * 41 + 40] == '\n';
            }
        }
    }
    return ok;
}

static bool sdl_pristine(const char* source)
{
    bool ok = true;
    for (char** variable = environ; *variable && ok; variable += 1)
    {
        ok = strncmp(*variable, "GIT_", 4) != 0;
    }
    char* identity[] = {"rev-parse", "--verify", "HEAD", NULL};
    if (ok)
    {
        ok = sdl_git("source-pin", source, identity) == 0 &&
            !strcmp(sdl_text, BUSTER_SDL_PIN "\n");
    }
    char* replacements[] = {"for-each-ref", "refs/replace", NULL};
    if (ok)
    {
        ok = sdl_git("source-replacements", source, replacements) == 0 && !sdl_text_size;
    }
    char* sparse[] = {"config", "--bool", "--get", "core.sparseCheckout", NULL};
    if (ok)
    {
        int configured = sdl_git("source-sparse", source, sparse);
        ok = (configured == 1 && !sdl_text_size) ||
            (configured == 0 && !strcmp(sdl_text, "false\n"));
    }
    char* flags[] = {"ls-files", "-v", "-z", NULL};
    if (ok)
    {
        ok = sdl_git("source-index-flags", source, flags) == 0 && sdl_text_size > 0;
        size_t offset = 0;
        while (ok && offset < sdl_text_size)
        {
            char* entry = sdl_text + offset;
            char* end = memchr(entry, 0, sdl_text_size - offset);
            ok = end && end - entry > 2 && entry[0] == 'H' && entry[1] == ' ';
            if (ok)
            {
                offset = (size_t)(end - sdl_text) + 1;
            }
        }
    }
    char* dirty[] = {"status", "--porcelain=v1", "--untracked-files=all",
        "--ignored=matching", NULL};
    if (ok)
    {
        ok = sdl_git("source-pristine", source, dirty) == 0 && !sdl_text_size;
    }
    if (ok)
    {
        ok = sdl_raw_objects(source);
    }
    if (!ok)
    {
        fprintf(stderr, "SDL_INPUT_REFUSED expected=%s source=%s\n", BUSTER_SDL_PIN, source);
    }
    return ok;
}

static int sdl_compiler(int argc, char** argv)
{
    const char* ide = getenv("BUSTER_SDL_IDE");
    const char* logs = getenv("BUSTER_SDL_CC_LOG");
    bool ok = ide && logs && argc + 2 < BUSTER_SDL_ARGS;
    char* args[BUSTER_SDL_ARGS];
    unsigned count = 0;
    if (ok)
    {
        args[count++] = (char*)ide;
        args[count++] = "cc";
        for (int i = 2; i < argc; i += 1)
        {
            args[count++] = argv[i];
        }
        args[count] = NULL;
    }
    char name[80], path[BUSTER_SDL_PATH];
    snprintf(name, sizeof(name), "invocation-%ld.argv", (long)getpid());
    if (ok)
    {
        sdl_path(path, logs, name);
        FILE* file = fopen(path, "wx");
        ok = file != NULL;
        if (ok)
        {
            ok = sdl_argv(file, args);
            ok = fclose(file) == 0 && ok;
        }
    }
    int result = 125;
    if (ok)
    {
        // Never replace a failed Buster compilation with trusted C objects.
        pid_t child = fork();
        if (child == 0)
        {
            execv(ide, args);
            perror("exec Buster C compiler");
            _exit(127);
        }
        else if (child > 0)
        {
            int status = 0;
            pid_t waited;
            do
            {
                waited = waitpid(child, &status, 0);
            } while (waited < 0 && errno == EINTR);
            if (waited == child && WIFEXITED(status))
            {
                result = WEXITSTATUS(status);
            }
            else if (waited == child && WIFSIGNALED(status))
            {
                result = 128 + WTERMSIG(status);
            }
            snprintf(name, sizeof(name), "invocation-%ld.status", (long)getpid());
            sdl_path(path, logs, name);
            FILE* status_file = fopen(path, "wx");
            if (status_file)
            {
                int written = fprintf(status_file, "exit_or_signal_status=%d\n", result);
                if (fclose(status_file) != 0 || written < 0)
                {
                    result = 125;
                }
            }
            else
            {
                result = 125;
            }
        }
    }
    return result;
}

// This diagnostic has no SDL dependency: Clang's foreign bool callee sees
// the client's nonnull pointer and must return false to every client.
static int sdl_bool_probe(const char* self)
{
    char callee[BUSTER_SDL_PATH];
    sdl_path(callee, sdl_out, "bool-callee.o");
    char* helper[] = {"clang", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
        "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-c",
        "tools/fixtures/sdl3_bool_callee.c", "-o", callee, NULL};
    int result = sdl_run("fixture-bool-callee-compile", helper, "30");
    char* disassemble[] = {"objdump", "-dr", callee, NULL};
    int assembly = result == 0 ? sdl_run("fixture-bool-callee-disassembly", disassemble, "20") : 125;
    bool all = result == 0 && assembly == 0;
    const char* compilers[] = {"clang", "gcc", self};
    const char* names[] = {"fixture-bool-clang", "fixture-bool-gcc", "fixture-bool-buster"};
    for (unsigned i = 0; result == 0 && i < 3; i += 1)
    {
        char object[BUSTER_SDL_PATH], binary[BUSTER_SDL_PATH], name[128];
        snprintf(name, sizeof(name), "%s.o", names[i]);
        sdl_path(object, sdl_out, name);
        sdl_path(binary, sdl_out, names[i]);
        char* compile[] = {(char*)compilers[i], i == 2 ? "--cc" : "-Wall",
            "-g", "-O0", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "-c",
            "tools/fixtures/sdl3_bool_caller.c", "-o", object, NULL};
        int built = sdl_run(names[i], compile, "30");
        char* link[] = {"clang", object, callee, "-o", binary, NULL};
        int linked = built == 0 ? sdl_run(names[i], link, "20") : 125;
        char* run[] = {binary, NULL};
        int ran = linked == 0 ? sdl_run(names[i], run, "10") : 125;
        all = all && built == 0 && linked == 0 && ran == 0;
    }
    return all ? 0 : 1;
}

static int sdl_configure(const char* source, const char* output, const char* compiler, bool candidate)
{
    char cc[BUSTER_SDL_PATH + 64];
    int length = snprintf(cc, sizeof(cc), "-DCMAKE_C_COMPILER=%s", compiler);
    bool ok = length >= 0 && length < (int)sizeof(cc);
    char* args[] = {
        "cmake", "-S", (char*)source, "-B", (char*)output, "-G", "Ninja", cc,
        "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON",
        "-DCMAKE_C_FLAGS=-fwrapv -fno-strict-aliasing -funsigned-char",
        "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST_LIBRARY=ON", "-DSDL_TESTS=ON",
        "-DSDL_TESTS_LINK_SHARED=ON", "-DSDL_INSTALL=OFF",
        "-DSDL_ASSEMBLY=OFF", "-DSDL_X11=ON", "-DSDL_X11_SHARED=OFF",
        "-DSDL_PULSEAUDIO=ON", "-DSDL_PULSEAUDIO_SHARED=OFF",
        "-DSDL_ALSA=OFF", "-DSDL_PIPEWIRE=OFF", "-DSDL_JACK=OFF", "-DSDL_SNDIO=OFF", "-DSDL_OSS=OFF",
        "-DSDL_WAYLAND=OFF", "-DSDL_KMSDRM=OFF", "-DSDL_OFFSCREEN=OFF",
        "-DSDL_OPENGL=OFF", "-DSDL_OPENGLES=OFF", "-DSDL_VULKAN=OFF", "-DSDL_GPU=OFF",
        "-DSDL_CAMERA=OFF", "-DSDL_JOYSTICK=ON", "-DSDL_HIDAPI=ON", "-DSDL_HIDAPI_JOYSTICK=OFF", "-DSDL_HIDAPI_LIBUSB=OFF", "-DSDL_HAPTIC=ON", "-DSDL_SENSOR=OFF",
        "-DSDL_LIBUDEV=OFF", "-DSDL_LIBURING=OFF", "-DSDL_DBUS=OFF", "-DSDL_IBUS=OFF",
        candidate ? "-DCMAKE_C_COMPILER_ARG1=--cc" : "-DCMAKE_C_COMPILER_ARG1=",
        NULL
    };
    int result = ok ? sdl_run(candidate ? "candidate-configure" : "reference-configure", args, "300") : 125;
    return result;
}

static int sdl_build(const char* output, const char* role)
{
    char* args[] = {"cmake", "--build", (char*)output, "--parallel", "2", "--target", "SDL3-shared", "testautomation", "testatomic", "testthread", "testsem", "testerror", "--", "-k", "0", "-v", NULL};
    int result = sdl_run(role, args, "600");
    return result;
}

static int sdl_tests(const char* output, const char* role)
{
    char* args[] = {"ctest", "--test-dir", (char*)output, "--output-on-failure", "--no-tests=error",
        "--timeout", "150", "-R", "^(testautomation|testatomic|testthread|testsem|testerror)$", NULL};
    int result = sdl_run(role, args, "300");
    char path[BUSTER_SDL_PATH];
    sdl_path(path, output, "Testing/Temporary/LastTest.log");
    if (sdl_read(path))
    {
        char* line = sdl_text;
        while (line && *line)
        {
            char* next = strchr(line, '\n');
            if (next)
            {
                *next++ = 0;
            }
            if (strstr(line, "Failed") || strstr(line, "Skipped") || strstr(line, "Summary"))
            {
                printf("SDL_UPSTREAM_DETAIL %s %s\n", role, line);
            }
            line = next;
        }
    }
    return result;
}

static int sdl_fixture_compile(const char* source, const char* compiler, bool candidate, char object[BUSTER_SDL_PATH])
{
    char include[BUSTER_SDL_PATH + 8];
    int n = snprintf(include, sizeof(include), "-I%s/include", source);
    sdl_path(object, sdl_out, candidate ? "fixture-buster.o" : "fixture-clang.o");
    char* trusted[] = {(char*)compiler, "-Wall", "-Wextra", "-Werror", "-g", "-O0",
        "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", include,
        "-c", "tools/fixtures/sdl3_fixture.c", "-o", object, NULL};
    char* buster[] = {(char*)compiler, "--cc", "-g", "-O0",
        "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", include,
        "-c", "tools/fixtures/sdl3_fixture.c", "-o", object, NULL};
    int result = n >= 0 && n < (int)sizeof(include) ?
        sdl_run(candidate ? "fixture-buster-compile" : "fixture-clang-compile", candidate ? buster : trusted, "60") : 125;
    return result;
}

static int sdl_fixture_run(const char* object, const char* library, const char* role)
{
    char library_arg[BUSTER_SDL_PATH + 8], rpath_arg[BUSTER_SDL_PATH + 32], binary[BUSTER_SDL_PATH];
    int a = snprintf(library_arg, sizeof(library_arg), "-L%s", library);
    int b = snprintf(rpath_arg, sizeof(rpath_arg), "-Wl,-rpath,%s", library);
    sdl_path(binary, sdl_out, role);
    char* link[] = {"clang", (char*)object, library_arg, rpath_arg, "-lSDL3", "-lX11", "-lXtst", "-lpulse-simple", "-lpulse", "-lm", "-o", binary, NULL};
    bool ok = a >= 0 && a < (int)sizeof(library_arg) && b >= 0 && b < (int)sizeof(rpath_arg);
    int result = ok ? sdl_run(role, link, "30") : 125;
    if (result == 0)
    {
        char* run[] = {binary, NULL};
        unsigned sequence = sdl_sequence;
        result = sdl_run(role, run, "60");
        char name[128], path[BUSTER_SDL_PATH];
        snprintf(name, sizeof(name), "%03u-%s.log", sequence, role);
        sdl_path(path, sdl_out, name);
        if (result == 0 && (!sdl_read(path) || !strstr(sdl_text,
            "SDL_FIXTURE x11_pixels=1 xtest_input=1 thread_exchange=1 audio_buffer=1 pulse_monitor=1 quit=1\n")))
        {
            fprintf(stderr, "SDL_FIXTURE_VERDICT_MISSING role=%s\n", role);
            result = 126;
        }
        if (result == 124 || result == 137)
        {
            char* debug[] = {"gdb", "--batch", "-ex", "set pagination off", "-ex", "set confirm off",
                "-ex", "set breakpoint pending on", "-ex", "break SDL_PollEvent", "-ex", "ignore 1 100",
                "-ex", "run", "-ex", "finish", "-ex", "info registers rax",
                "-ex", "thread apply all bt", "-ex", "disassemble /r sdl_input", "--args", binary, NULL};
            int diagnostic = sdl_run("fixture-input-poll-breakpoint-diagnostic", debug, "20");
            fprintf(stderr, "SDL_DEBUGGER_DIAGNOSTIC status=%d primary_status=%d\n", diagnostic, result);
        }
    }
    return result;
}

int main(int argc, char** argv)
{
    int result = 2;
    if (argc >= 2 && !strcmp(argv[1], "--cc"))
    {
        result = sdl_compiler(argc, argv);
    }
    else if (argc == 4)
    {
        char source[BUSTER_SDL_PATH], self[BUSTER_SDL_PATH];
        static char output[BUSTER_SDL_PATH];
        bool checking = !strcmp(argv[1], "--check");
        const char* input = checking ? argv[2] : argv[1];
        bool ok = realpath(input, source) && realpath(argv[0], self) && mkdir(argv[3], 0700) == 0 && realpath(argv[3], output);
        sdl_out = output;
        if (ok)
        {
            ok = sdl_pristine(source);
        }
        if (checking)
        {
            result = ok ? 0 : 1;
        }
        else if (ok)
        {
            char compiler[BUSTER_SDL_PATH], cc_logs[BUSTER_SDL_PATH], reference[BUSTER_SDL_PATH], candidate[BUSTER_SDL_PATH];
            ok = realpath(argv[2], compiler) != NULL;
            sdl_path(cc_logs, output, "c-commands");
            sdl_path(reference, output, "reference");
            sdl_path(candidate, output, "candidate");
            ok = ok && mkdir(cc_logs, 0700) == 0 && setenv("BUSTER_SDL_IDE", compiler, 1) == 0 && setenv("BUSTER_SDL_CC_LOG", cc_logs, 1) == 0;
            char* versions[] = {"clang", "--version", NULL};
            char* hashes[] = {"sha256sum", compiler, self, "tools/sdl3_qualification.c", "tools/fixtures/sdl3_fixture.c", "tools/fixtures/sdl3_bool_callee.c", "tools/fixtures/sdl3_bool_caller.c", "LICENSES/README.md", NULL};
            char* packages[] = {"dpkg-query", "-W", NULL};
            char* inputs[] = {"git", "-C", source, "ls-tree", "-r", "HEAD", NULL};
            if (ok)
            {
                ok = sdl_run("reference-tool", versions, "20") == 0 &&
                     sdl_run("compiler-identities", hashes, "30") == 0 &&
                     sdl_run("dependency-inventory", packages, "30") == 0 &&
                     sdl_run("upstream-inputs", inputs, "30") == 0;
            }
            int bool_probe = ok ? sdl_bool_probe(self) : 125;
            int reference_config = ok ? sdl_configure(source, reference, "clang", false) : 125;
            int reference_build = reference_config == 0 ? sdl_build(reference, "reference-build") : 125;
            int reference_tests = reference_build == 0 ? sdl_tests(reference, "reference-tests") : 125;
            char trusted_object[BUSTER_SDL_PATH], buster_object[BUSTER_SDL_PATH];
            int trusted_client = ok ? sdl_fixture_compile(source, "clang", false, trusted_object) : 125;
            int buster_client = ok ? sdl_fixture_compile(source, self, true, buster_object) : 125;
            int trusted_reference = reference_build == 0 && trusted_client == 0 ?
                sdl_fixture_run(trusted_object, reference, "fixture-clang-reference") : 125;
            int buster_reference = reference_build == 0 && buster_client == 0 ?
                sdl_fixture_run(buster_object, reference, "fixture-buster-reference") : 125;
            int candidate_config = ok ? sdl_configure(source, candidate, self, true) : 125;
            int candidate_build = candidate_config == 0 ? sdl_build(candidate, "candidate-build") : 125;
            int candidate_tests = candidate_build == 0 ? sdl_tests(candidate, "candidate-tests") : 125;
            int trusted_candidate = candidate_build == 0 && trusted_client == 0 ?
                sdl_fixture_run(trusted_object, candidate, "fixture-clang-candidate") : 125;
            int buster_candidate = candidate_build == 0 && buster_client == 0 ?
                sdl_fixture_run(buster_object, candidate, "fixture-buster-candidate") : 125;
            bool pristine_after = ok && sdl_pristine(source);
            char path[BUSTER_SDL_PATH];
            sdl_path(path, output, "qualification.txt");
            FILE* report = fopen(path, "wx");
            bool saved = report != NULL;
            if (saved)
            {
                int n = fprintf(report,
                    "upstream=%s\nreference_configure=%d\nreference_build=%d\nreference_tests=%d\n"
                    "candidate_configure=%d\ncandidate_build=%d\ncandidate_tests=%d\n"
                    "pristine_after=%d\nnot_executed_status=125\nforeign_bool_diagnostic=%d\n"
                    "trusted_client_compile=%d\nbuster_client_compile=%d\n"
                    "trusted_reference=%d\nbuster_reference=%d\ntrusted_candidate=%d\nbuster_candidate=%d\n"
                    "gpu=pending\naudible_device=pending\nperformance_9700x=pending\n",
                    BUSTER_SDL_PIN, reference_config, reference_build, reference_tests,
                    candidate_config, candidate_build, candidate_tests, pristine_after, bool_probe,
                    trusted_client, buster_client, trusted_reference, buster_reference, trusted_candidate, buster_candidate);
                saved = fclose(report) == 0 && n >= 0;
            }
            result = ok && saved && pristine_after && bool_probe == 0 && reference_config == 0 && reference_build == 0 &&
                     reference_tests == 0 && candidate_config == 0 && candidate_build == 0 && candidate_tests == 0 &&
                     trusted_reference == 0 && buster_reference == 0 && trusted_candidate == 0 && buster_candidate == 0 ? 0 : 1;
        }
        else
        {
            result = 1;
        }
    }
    else
    {
        fprintf(stderr, "usage: sdl3_qualification UPSTREAM IDE FRESH_EVIDENCE\n       sdl3_qualification --check UPSTREAM FRESH_EVIDENCE\n");
    }
    return result;
}
