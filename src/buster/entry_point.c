#include <buster/entry_point.h>
#include <buster/system_headers.h>
#include <buster/target.h>
#include <buster/arena.h>
#include <buster/string.h>

BUSTER_V_IMPL OsState os_state;

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZ
BUSTER_EXPORT s32 LLVMFuzzerTestOneInput(const u8* pointer, size_t size)
{
    return buster_fuzz(pointer, size);
}
#else
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
    BooleanArgumentProcessResult process_result = boolean_argument_process(flag_string_starts, BUSTER_ARRAY_LENGTH(flag_string_starts), program_state->input.flags, PROGRAM_FLAG_COUNT, argument);
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

#if defined(__linux__)
BUSTER_GLOBAL_LOCAL void signal_handler(int sig, siginfo_t *info, void *arg)
{
    BUSTER_UNUSED(sig);
    BUSTER_UNUSED(info);
    BUSTER_UNUSED(arg);
}
#endif

BUSTER_GLOBAL_LOCAL void install_signal_handlers(void)
{
#if defined(__linux__)
      // install signal handler for the crash call stacks
  {
    struct sigaction handler = { .sa_sigaction = &signal_handler, .sa_flags = SA_SIGINFO, };
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

BUSTER_GLOBAL_LOCAL ProcessResult buster_entry_point(StringOsList argv, StringOsList envp)
{
    os_state.arena = arena_create((ArenaCreation){0});
    SliceString8 environments_raw;
#if defined(_WIN32)
    SliceString8 arguments = slice_string_from_windows_string_list(os_state.arena, argv);
    environments_raw = string16_environment_block_to_slice_string(os_state.arena, envp);
#else
    SliceString8 arguments = slice_string_from_posix_string_list(os_state.arena, argv);
    environments_raw = slice_string_from_posix_string_list(os_state.arena, envp);
#endif

    SliceString8 environment_keys = { .pointer = arena_allocate(os_state.arena, String8, environments_raw.length), .length = environments_raw.length };
    SliceString8 environment_values = { .pointer = arena_allocate(os_state.arena, String8, environments_raw.length), .length = environments_raw.length };

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
    {
        LARGE_INTEGER i;
        if (QueryPerformanceFrequency(&i))
        {
            os_state.frequency = (u64)i.QuadPart;
        }
    }
    WSADATA WinSockData;
    WSAStartup(MAKEWORD(2, 2), &WinSockData);
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

    return result;
}

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
#else
    result = (int)buster_entry_point((StringOsList)argv, (StringOsList)envp);
#endif
    return result;
}

#if BUSTER_ANDROID
#include <buster/window.h>
#include <buster/file.h>
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

        if (value_string) { (*env)->DeleteLocalRef(env, value_string); }
        if (key_string) { (*env)->DeleteLocalRef(env, key_string); }
        if (intent_class) { (*env)->DeleteLocalRef(env, intent_class); }
        if (intent) { (*env)->DeleteLocalRef(env, intent); }
        if (activity_class) { (*env)->DeleteLocalRef(env, activity_class); }
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

    Arena* android_arg_arena = arena_create((ArenaCreation){0});
    String8 android_args = buster_android_intent_string_extra(android_arg_arena, app, "buster_args");
    char* android_argv[32] = { (char*)"buster-ide" };
    buster_android_append_argv(android_args, android_argv, BUSTER_ARRAY_LENGTH(android_argv));
    char* android_envp[] = { 0 };

    ProcessResult result = buster_entry_point((StringOsList)android_argv, (StringOsList)android_envp);

    __android_log_print(ANDROID_LOG_INFO, "buster", "BUSTER_ANDROID_TEST_RESULT:%u", (unsigned)result);
    __android_log_write(ANDROID_LOG_INFO, "buster", "android_main returning (entry point finished)");
    arena_destroy(android_arg_arena, 1);
}
#endif
#endif
#else
#if defined(_WIN32)
[[gnu::noreturn]] BUSTER_EXPORT void mainCRTStartup()
{
    CharOs* environment = GetEnvironmentStringsW();
    let result = buster_entry_point(GetCommandLineW(), environment);
    if (environment)
    {
        FreeEnvironmentStringsW(environment);
    }
    ExitProcess((UINT)result);
}
#endif

BUSTER_EXPORT void *memset(void* pointer, int c, size_t n)
{
    u8* restrict p = (u8*)pointer;

    for (u64 i = 0; i < n; i += 1)
    {
        p[i] = c;
    }

    return pointer;
}

BUSTER_EXPORT int memcmp(const void* s1, const void* s2, size_t n)
{
    let a = (u8*)s1;
    let b = (u8*)s2;

    int result = 0;

    for (u64 i = 0; i < n; i += 1)
    {
        result = a - b;
        if (result)
        {
            break;
        }
    }

    return result;
}

BUSTER_EXPORT void *memcpy(void* restrict destination, const void* restrict source, size_t n)
{
    for (u64 i = 0; i < n; i += 1)
    {
        ((u8*)(destination))[i] = ((u8*)(source))[i];
    }

    return destination;
}

BUSTER_EXPORT size_t strlen(const char* s)
{
    let i = s;
    let it = (char*)s;

    while (*it)
    {
        it += 1;
    }

    return (size_t)(it - i);
}

[[gnu::noreturn]] BUSTER_EXPORT void abort()
{
    BUSTER_TRAP();
}

BUSTER_EXPORT int atoi(const char* pointer)
{
    return parse_decimal_scalar(pointer);
}

BUSTER_EXPORT long atol(const char* pointer)
{
    return parse_decimal_scalar(pointer);
}

BUSTER_EXPORT long long atoll(const char* pointer)
{
    return parse_decimal_scalar(pointer);
}

BUSTER_EXPORT double frexp(double x, int* e)
{
    // TODO:
    BUSTER_UNUSED(x);
    BUSTER_UNUSED(e);
    return 0.0;
}

[[gnu::noreturn]] BUSTER_EXPORT void longjmp(jmp_buf env, int val)
{
    // TODO:
    BUSTER_TRAP();
}

BUSTER_EXPORT void *memchr(const void* s, int c, size_t n)
{
    u8* pointer = s;
    u8 ch = (u8)c;
    u64 i;
    for (i = 0; i < n; i += 1)
    {
        if (pointer[i] == ch)
        {
            break;
        }
    }

    return pointer + i == pointer + n ? 0 : pointer + i;
}

BUSTER_IMPL int _fltused = 1;

#if defined(_WIN32)
BUSTER_EXPORT void __chkstk(void)
{
}
#endif
#endif
