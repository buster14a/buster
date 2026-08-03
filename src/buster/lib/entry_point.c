#include <buster/lib/entry_point.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/target.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>

BUSTER_V_IMPL OsState os_state;

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZ_AVAILABLE
extern int LLVMFuzzerRunDriver(int* argc, char*** argv, int (*callback)(const u8* pointer, size_t size));
#if !BUSTER_SANITIZE
BUSTER_GLOBAL_LOCAL void restore_default_signal_handlers(void);
#endif
ProcessResult buster_fuzz_run(SliceString8 fuzz_arguments)
{
    if (fuzz_arguments.length > (u64)INT32_MAX - 1)
    {
        return PROCESS_RESULT_FAILED;
    }

#if !BUSTER_SANITIZE
    // libFuzzer's SetSigaction() refuses to replace an already-installed
    // handler, so our crash reporter would otherwise silently disable
    // libFuzzer's own crash reporting: no crash-<sha1> artifact, no reproducer
    // command, no minimization. A fuzz session with no saved input is not
    // actionable, so hand the fatal signals back before entering the driver.
    restore_default_signal_handlers();
#endif

    u64 argument_count = fuzz_arguments.length + 1;
    char** arguments = arena_allocate(program_state->arena, char*, argument_count + 1);
    String8 executable = string_duplicate_arena(program_state->arena, program_state->input.arguments.pointer[0], true);
    arguments[0] = (char*)executable.pointer;
    for (u64 i = 0; i < fuzz_arguments.length; i += 1)
    {
        String8 argument = string_duplicate_arena(program_state->arena, fuzz_arguments.pointer[i], true);
        arguments[i + 1] = (char*)argument.pointer;
    }
    arguments[argument_count] = 0;

    int argc = (int)argument_count;
    int result = LLVMFuzzerRunDriver(&argc, &arguments, &buster_fuzz_test_input);
    return result == 0 ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
#endif

ProcessResult buster_argument_process(u64 argument_index)
{
    String8 argument = program_state->input.arguments.pointer[argument_index];

    String8 flag_string_starts[] = {
        [PROGRAM_FLAG_VERBOSE] = S8("--verbose="),
        [PROGRAM_FLAG_CI] = S8("--ci="),
        [PROGRAM_FLAG_TEST_PERSIST] = S8("--test-persist="),
    };

    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(flag_string_starts) == PROGRAM_FLAG_COUNT);

    ProcessResult result = PROCESS_RESULT_FAILED;
    BooleanArgumentProcessResult process_result =
        boolean_argument_process(flag_string_starts, BUSTER_ARRAY_LENGTH(flag_string_starts), program_state->input.flags, PROGRAM_FLAG_COUNT, argument);
    if (process_result.valid && process_result.index < (u64)PROGRAM_FLAG_COUNT)
    {
        result = PROCESS_RESULT_SUCCESS;
    }

    return result;
}

bool update(void)
{
#if BUSTER_USE_GRAPHICS
    bool result = frame();
    return result;
#else
    return false;
#endif
}

#if !BUSTER_SANITIZE
#if defined(__linux__)
// Everything below runs inside a signal handler for a fatal signal, so it must
// be async-signal-safe: raw write(2) into a stack buffer, no allocation, no
// locks, no stdio.
BUSTER_GLOBAL_LOCAL u64 crash_report_append_string(char8* buffer, u64 length, u64 capacity, String8 string)
{
    for (u64 i = 0; i < string.length && length < capacity; i += 1, length += 1)
    {
        buffer[length] = string.pointer[i];
    }
    return length;
}

BUSTER_GLOBAL_LOCAL u64 crash_report_append_hex_u64(char8* buffer, u64 length, u64 capacity, u64 value)
{
    length = crash_report_append_string(buffer, length, capacity, S8("0x"));
    for (u32 i = 0; i < 16 && length < capacity; i += 1, length += 1)
    {
        u64 digit = (value >> (60 - i * 4)) & 0xf;
        buffer[length] = (char8)(digit > 9 ? ('a' + (digit - 10)) : ('0' + digit));
    }
    return length;
}

BUSTER_GLOBAL_LOCAL String8 crash_signal_name(int sig)
{
    switch (sig)
    {
    case SIGILL:
        return S8("SIGILL");
    case SIGTRAP:
        return S8("SIGTRAP");
    case SIGABRT:
        return S8("SIGABRT");
    case SIGFPE:
        return S8("SIGFPE");
    case SIGBUS:
        return S8("SIGBUS");
    case SIGSEGV:
        return S8("SIGSEGV");
    case SIGQUIT:
        return S8("SIGQUIT");
    default:
        return S8("signal");
    }
}

BUSTER_GLOBAL_LOCAL void signal_handler(int sig, siginfo_t* info, void* arg)
{
    u64 pc = 0;
#if BUSTER_CPU_ARCH_X86_64
    ucontext_t* context = (ucontext_t*)arg;
    pc = (u64)context->uc_mcontext.gregs[REG_RIP];
#elif BUSTER_CPU_ARCH_AARCH64
    ucontext_t* context = (ucontext_t*)arg;
    pc = (u64)context->uc_mcontext.pc;
#else
    BUSTER_UNUSED(arg);
#endif

    char8 buffer[256];
    u64 length = 0;
    length = crash_report_append_string(buffer, length, sizeof(buffer), S8("fatal signal "));
    length = crash_report_append_string(buffer, length, sizeof(buffer), crash_signal_name(sig));
    length = crash_report_append_string(buffer, length, sizeof(buffer), S8(" at address "));
    length = crash_report_append_hex_u64(buffer, length, sizeof(buffer), (u64)info->si_addr);
    length = crash_report_append_string(buffer, length, sizeof(buffer), S8(", pc "));
    length = crash_report_append_hex_u64(buffer, length, sizeof(buffer), pc);
    length = crash_report_append_string(buffer, length, sizeof(buffer), S8("\n"));

    ssize_t write_result = write(STDERR_FILENO, buffer, length);
    BUSTER_UNUSED(write_result);

    // Restore the default disposition and re-raise so the process still dies
    // with this signal (core dump, correct exit status for CI). Returning from
    // a handler for a synchronous fault would re-execute the faulting
    // instruction forever.
    struct sigaction default_action = {0};
    default_action.sa_handler = SIG_DFL;
    sigaction(sig, &default_action, 0);
    raise(sig);
}
#endif

BUSTER_GLOBAL_LOCAL void install_signal_handlers(void)
{
#if defined(__linux__)
    // install signal handler for the crash call stacks
    {
        struct sigaction handler = {
            .sa_sigaction = &signal_handler,
            .sa_flags = SA_SIGINFO,
        };
        sigfillset(&handler.sa_mask);
        sigaction(SIGILL, &handler, NULL);
        sigaction(SIGTRAP, &handler, NULL);
        sigaction(SIGABRT, &handler, NULL);
        sigaction(SIGFPE, &handler, NULL);
        sigaction(SIGBUS, &handler, NULL);
        sigaction(SIGSEGV, &handler, NULL);
        sigaction(SIGQUIT, &handler, NULL);
    }
#else
#endif
}

#if BUSTER_FUZZ_AVAILABLE
BUSTER_GLOBAL_LOCAL void restore_default_signal_handlers(void)
{
#if defined(__linux__)
    struct sigaction default_action = {0};
    default_action.sa_handler = SIG_DFL;
    sigaction(SIGILL, &default_action, NULL);
    sigaction(SIGTRAP, &default_action, NULL);
    sigaction(SIGABRT, &default_action, NULL);
    sigaction(SIGFPE, &default_action, NULL);
    sigaction(SIGBUS, &default_action, NULL);
    sigaction(SIGSEGV, &default_action, NULL);
    sigaction(SIGQUIT, &default_action, NULL);
#endif
}
#endif
#endif

BUSTER_GLOBAL_LOCAL ProcessResult buster_entry_point(StringOsList argv, StringOsList envp)
{
#if !BUSTER_SANITIZE
    // Sanitizer runtimes install their own fault handlers with much richer
    // reports; only take over crash reporting in non-sanitized builds.
    install_signal_handlers();
#endif
    os_state.arena = arena_create((ArenaCreation){0});
    SliceString8 environments_raw;
#if defined(_WIN32)
    SliceString8 arguments = slice_string_from_windows_string_list(os_state.arena, argv);
    environments_raw = string16_environment_block_to_slice_string(os_state.arena, envp);
#else
    SliceString8 arguments = slice_string_from_posix_string_list(os_state.arena, argv);
    environments_raw = slice_string_from_posix_string_list(os_state.arena, envp);
#endif

    SliceString8 environment_keys = {.pointer = arena_allocate(os_state.arena, String8, environments_raw.length), .length = environments_raw.length};
    SliceString8 environment_values = {.pointer = arena_allocate(os_state.arena, String8, environments_raw.length), .length = environments_raw.length};

    for (u64 i = 0; i < environments_raw.length; i += 1)
    {
        String8 environment_raw = environments_raw.pointer[i];
        u64 equal_index = string_first_code_unit(environment_raw, '=');
        if (equal_index != BUSTER_STRING_NO_MATCH)
        {
            String8 key = string_slice(environment_raw, 0, equal_index);
            String8 value = string_slice(environment_raw, equal_index + 1, environment_raw.length);
            environment_keys.pointer[i] = key;
            environment_values.pointer[i] = value;
        }
        else
        {
            environment_keys.pointer[i] = (String8){0};
            environment_values.pointer[i] = (String8){0};
        }
    }

#if defined(_WIN32)
    bool winsock_initialized = false;
    {
        LARGE_INTEGER i;
        if (QueryPerformanceFrequency(&i))
        {
            os_state.frequency = (u64)i.QuadPart;
        }
    }
    WSADATA WinSockData;
    winsock_initialized = WSAStartup(MAKEWORD(2, 2), &WinSockData) == 0;
#if defined(_MSC_VER)
    GUID guid = WSAID_MULTIPLE_RIO;
    DWORD rio_byte = 0;
    SOCKET Sock = socket(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
    WSAIoctl(Sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), (void**)&w32_rio_functions, sizeof(w32_rio_functions), &rio_byte, 0, 0);
    closesocket(Sock);
#endif
#endif
    os_state.entity_arena = arena_create((ArenaCreation){0});
#if defined(__linux__) || defined(__APPLE__)
    pthread_mutex_init(&os_state.entity_mutex, 0);
#elif defined(_WIN32)
    InitializeCriticalSection(&os_state.entity_mutex);
#endif
    os_state.logical_thread_count = os_get_logical_thread_count();
    os_state.page_size = os_get_page_size();
    os_state.large_page_size = BUSTER_MB(2);
    os_state.allocation_granularity = os_state.page_size;
    os_state.process.handle = os_get_current_process_handle();

    cpu_detect_model();

    ThreadContext* thread_context = thread_context_allocate();
    thread_context_select(thread_context);
    os_thread_set_name(S8("main_thread"));

    program_state->arena = arena_create((ArenaCreation){0});
    program_state->input.arguments = arguments;
    program_state->input.environment_keys = environment_keys;
    program_state->input.environment_values = environment_values;
    program_state->input.raw_arguments = argv;
    program_state->input.raw_environment = envp;

    ProcessResult result = process_arguments();

    if (result == PROCESS_RESULT_SUCCESS)
    {
        result = entry_point();
    }

#if !BUSTER_IOS && !BUSTER_ANDROID
    // Mobile entry points terminate or return through their platform-owned
    // process lifecycle after this function. In particular, the iOS worker
    // still emits its CI result marker, so it must retain the thread context.
    Arena* program_arena = program_state->arena;
    program_state->arena = 0;
    program_state->input = (ProgramInput){0};
    arena_destroy(program_arena, 1);

    thread_context_release(thread_context);

#if defined(__linux__) || defined(__APPLE__)
    pthread_mutex_destroy(&os_state.entity_mutex);
#elif defined(_WIN32)
    DeleteCriticalSection(&os_state.entity_mutex);
#endif
    Arena* entity_arena = os_state.entity_arena;
    Arena* os_arena = os_state.arena;
    memset(&os_state, 0, sizeof(os_state));
    arena_destroy(entity_arena, 1);
    arena_destroy(os_arena, 1);
#if defined(_WIN32)
    if (winsock_initialized)
    {
        WSACleanup();
    }
#endif
#endif

    return result;
}

#if BUSTER_IOS
#include <buster/lib/window.h>
// argv/envp captured in main() so the worker thread can forward them to the
// real entry point once UIKit has finished launching.
BUSTER_GLOBAL_LOCAL char** buster_ios_argv = 0;
BUSTER_GLOBAL_LOCAL char** buster_ios_envp = 0;
#endif

int main(int argc, char* argv[], char* envp[])
{
    BUSTER_UNUSED(argc);
    int result;
#if defined(_WIN32)
    BUSTER_UNUSED(argv);
    BUSTER_UNUSED(envp);
    CharOs* environment = GetEnvironmentStringsW();
    result = (int)buster_entry_point(GetCommandLineW(), environment);
    if (environment)
    {
        FreeEnvironmentStringsW(environment);
    }
#elif BUSTER_IOS
    // UIApplicationMain owns the main thread/run loop and never returns; the IDE
    // loop runs on a worker thread started from the app delegate.
    buster_ios_argv = argv;
    buster_ios_envp = envp;
    buster_ios_application_main(argc, argv);
    result = 0;
#else
    result = (int)buster_entry_point((StringOsList)argv, (StringOsList)envp);
#endif
    return result;
}

#if BUSTER_IOS
#include <stdlib.h>

// Worker-thread trampoline started by the app delegate (window.c). Runs the IDE
// loop, then terminates the process so the simulator app exits and CI can read
// the test result (the IDE loop is otherwise expected to run indefinitely).
ProcessResult buster_ios_worker_entry(void)
{
    char* fallback_argv[] = {(char*)"buster-ide", 0};
    char* fallback_envp[] = {0};
    char** ios_argv = buster_ios_argv ? buster_ios_argv : fallback_argv;
    char** ios_envp = buster_ios_envp ? buster_ios_envp : fallback_envp;

    ProcessResult result = buster_entry_point((StringOsList)ios_argv, (StringOsList)ios_envp);
    // Deterministic marker the simulator launch script greps for (the app's exit
    // code is not reliably observable through simctl).
    string_print(result == PROCESS_RESULT_SUCCESS ? S8("BUSTER_IOS_RESULT: SUCCESS\n") : S8("BUSTER_IOS_RESULT: FAILURE\n"));
    exit((int)result);
    return result;
}
#endif

#if BUSTER_ANDROID
#include <buster/lib/window.h>
#include <buster/lib/file.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#endif
#include <android_native_app_glue.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// Android drops process stdout/stderr; pump it into logcat so string_print and
// crashes are visible under `adb logcat -s buster`.
BUSTER_GLOBAL_LOCAL void* buster_android_stdio_pump(void* arg)
{
    int fd = (int)(intptr_t)arg;
    char buffer[512];
    char line[1024];
    u64 line_length = 0;

    for (;;)
    {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count <= 0)
        {
            break;
        }

        for (ssize_t i = 0; i < count; i += 1)
        {
            char c = buffer[i];
            if (c == '\n' || line_length == sizeof(line) - 1)
            {
                line[line_length] = 0;
                __android_log_write(ANDROID_LOG_INFO, "buster", line);
                line_length = 0;
            }
            else if (c != '\r')
            {
                line[line_length] = c;
                line_length += 1;
            }
        }
    }

    return 0;
}

BUSTER_GLOBAL_LOCAL void buster_android_redirect_stdio_to_logcat(void)
{
    int pipe_fds[2];
    if (pipe(pipe_fds) == 0)
    {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);

        pthread_t thread;
        if (pthread_create(&thread, 0, buster_android_stdio_pump, (void*)(intptr_t)pipe_fds[0]) == 0)
        {
            pthread_detach(thread);
        }
    }
}

BUSTER_GLOBAL_LOCAL String8 buster_android_intent_string_extra(Arena* arena, struct android_app* app, const char* key)
{
    String8 result = {0};

    JavaVM* vm = app->activity->vm;
    JNIEnv* env = 0;
    bool detach = false;
    jint get_env_result = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (get_env_result == JNI_EDETACHED)
    {
        if ((*vm)->AttachCurrentThread(vm, &env, 0) == JNI_OK)
        {
            detach = true;
        }
    }

    if (env)
    {
        jobject activity = app->activity->clazz;
        jclass activity_class = (*env)->GetObjectClass(env, activity);
        jmethodID get_intent = activity_class ? (*env)->GetMethodID(env, activity_class, "getIntent", "()Landroid/content/Intent;") : 0;
        jobject intent = get_intent ? (*env)->CallObjectMethod(env, activity, get_intent) : 0;
        jclass intent_class = intent ? (*env)->GetObjectClass(env, intent) : 0;
        jmethodID get_string_extra = intent_class ? (*env)->GetMethodID(env, intent_class, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;") : 0;
        jstring key_string = get_string_extra ? (*env)->NewStringUTF(env, key) : 0;
        jstring value_string = key_string ? (jstring)(*env)->CallObjectMethod(env, intent, get_string_extra, key_string) : 0;

        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
            result = (String8){0};
        }
        else if (value_string)
        {
            const char* value = (*env)->GetStringUTFChars(env, value_string, 0);
            if (value)
            {
                result = string_duplicate_arena(arena, string_from_pointer((const char8*)value), true);
                (*env)->ReleaseStringUTFChars(env, value_string, value);
            }
        }

        if (value_string)
        {
            (*env)->DeleteLocalRef(env, value_string);
        }
        if (key_string)
        {
            (*env)->DeleteLocalRef(env, key_string);
        }
        if (intent_class)
        {
            (*env)->DeleteLocalRef(env, intent_class);
        }
        if (intent)
        {
            (*env)->DeleteLocalRef(env, intent);
        }
        if (activity_class)
        {
            (*env)->DeleteLocalRef(env, activity_class);
        }
    }

    if (detach)
    {
        (*vm)->DetachCurrentThread(vm);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void buster_android_append_argv(String8 arguments, char** argv, u64 argv_capacity)
{
    u64 argc = 1;
    char* cursor = (char*)arguments.pointer;
    while (cursor && *cursor && argc + 1 < argv_capacity)
    {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')
        {
            *cursor = 0;
            cursor += 1;
        }

        if (!*cursor)
        {
            break;
        }

        argv[argc++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\n' && *cursor != '\r')
        {
            cursor += 1;
        }
    }

    if (argc < argv_capacity)
    {
        argv[argc] = 0;
    }
}

// NativeActivity entry point: the glue spawns a thread that calls this.
void android_main(struct android_app* app)
{
    buster_android_redirect_stdio_to_logcat();

    buster_android_app = app;
    buster_android_asset_manager = app->activity->assetManager;
    buster_android_internal_data_path = app->activity->internalDataPath ? string_from_pointer((char8 const*)app->activity->internalDataPath) : (String8){0};

    Arena* android_arg_arena = arena_create((ArenaCreation){0});
    String8 android_args = buster_android_intent_string_extra(android_arg_arena, app, "buster_args");
    char* android_argv[32] = {(char*)"buster-ide"};
    buster_android_append_argv(android_args, android_argv, BUSTER_ARRAY_LENGTH(android_argv));
    char* android_envp[] = {0};

    ProcessResult result = buster_entry_point((StringOsList)android_argv, (StringOsList)android_envp);

    __android_log_print(ANDROID_LOG_INFO, "buster", "BUSTER_ANDROID_TEST_RESULT:%u", (unsigned)result);
    __android_log_write(ANDROID_LOG_INFO, "buster", "android_main returning (entry point finished)");
    arena_destroy(android_arg_arena, 1);
}
#endif
#else
// Some libc headers (glibc) wrap these in function-like macros; the
// replacement definitions below need the plain names.
#undef memset
#undef memcmp
#undef memcpy
#undef memchr
#undef strlen

#if defined(_WIN32)
BUSTER_NORETURN BUSTER_EXPORT void mainCRTStartup(void)
{
    CharOs* environment = GetEnvironmentStringsW();
    ProcessResult result = buster_entry_point(GetCommandLineW(), environment);
    if (environment)
    {
        FreeEnvironmentStringsW(environment);
    }
    ExitProcess((UINT)result);
}
#endif

BUSTER_EXPORT void* memset(void* pointer, int c, size_t n)
{
    u8* restrict p = (u8*)pointer;

    for (u64 i = 0; i < n; i += 1)
    {
        p[i] = (u8)c;
    }

    return pointer;
}

BUSTER_EXPORT int memcmp(const void* s1, const void* s2, size_t n)
{
    const u8* a = (const u8*)s1;
    const u8* b = (const u8*)s2;

    int result = 0;

    for (u64 i = 0; i < n; i += 1)
    {
        result = (int)a[i] - (int)b[i];
        if (result)
        {
            break;
        }
    }

    return result;
}

BUSTER_EXPORT void* memcpy(void* restrict destination, const void* restrict source, size_t n)
{
    for (u64 i = 0; i < n; i += 1)
    {
        ((u8*)(destination))[i] = ((const u8*)(source))[i];
    }

    return destination;
}

BUSTER_EXPORT size_t strlen(const char* s)
{
    const char* it = s;

    while (*it)
    {
        it += 1;
    }

    return (size_t)(it - s);
}

BUSTER_NORETURN BUSTER_EXPORT void abort(void)
{
    BUSTER_TRAP();
}

BUSTER_GLOBAL_LOCAL long long parse_decimal_scalar(const char* pointer)
{
    const char* it = pointer;

    while (*it == ' ' || *it == '\t' || *it == '\n' || *it == '\r')
    {
        it += 1;
    }

    bool negative = *it == '-';
    it += negative || *it == '+';

    long long result = 0;
    while (*it >= '0' && *it <= '9')
    {
        result = result * 10 + (*it - '0');
        it += 1;
    }

    return negative ? -result : result;
}

BUSTER_EXPORT int atoi(const char* pointer)
{
    return (int)parse_decimal_scalar(pointer);
}

BUSTER_EXPORT long atol(const char* pointer)
{
    return (long)parse_decimal_scalar(pointer);
}

BUSTER_EXPORT long long atoll(const char* pointer)
{
    return parse_decimal_scalar(pointer);
}

BUSTER_EXPORT double frexp(double x, int* e)
{
    u64 bits;
    memcpy(&bits, &x, sizeof(bits));
    u64 exponent_bits = (bits >> 52) & 0x7ff;

    if (exponent_bits == 0x7ff || x == 0.0)
    {
        // Inf/NaN: return x with *e unspecified (0 here). Zero: (0, 0).
        *e = 0;
        return x;
    }

    if (exponent_bits == 0)
    {
        // Subnormal: scale into the normal range first, then adjust the
        // exponent by the scale factor.
        double scaled = x * 18446744073709551616.0; // 2^64
        memcpy(&bits, &scaled, sizeof(bits));
        exponent_bits = (bits >> 52) & 0x7ff;
        *e = (int)exponent_bits - 1022 - 64;
    }
    else
    {
        *e = (int)exponent_bits - 1022;
    }

    // Force the exponent field to 1022 so the mantissa lands in [0.5, 1).
    bits = (bits & ~((u64)0x7ff << 52)) | ((u64)0x3fe << 52);
    double result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

BUSTER_EXPORT void* memchr(const void* s, int c, size_t n)
{
    const u8* pointer = (const u8*)s;
    u8 ch = (u8)c;
    u64 i;
    for (i = 0; i < n; i += 1)
    {
        if (pointer[i] == ch)
        {
            break;
        }
    }

    return i == n ? 0 : (void*)(pointer + i);
}

#if defined(_WIN32)
// Referenced by MSVC-compiled objects that use floating point.
int _fltused = 1;

BUSTER_EXPORT void __chkstk(void)
{
}
#endif
#endif
