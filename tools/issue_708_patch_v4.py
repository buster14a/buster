from pathlib import Path
from textwrap import dedent

path = Path("src/buster/tests/compiler/driver/driver_test.c")
text = path.read_text()
marker = "NATIVE_FRAME_VECTOR_LINK_CACHE_V1"
if marker in text:
    raise SystemExit(0)

function_marker = "BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_native_frame_vectors(UnitTestArguments* arguments)\n"
next_marker = "\nBUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_sysv_sseup(UnitTestArguments* arguments)"
if text.count(function_marker) != 1:
    raise SystemExit("native frame vector function marker changed")
function_start = text.index(function_marker)
function_end = text.index(next_marker, function_start)
function = text[function_start:function_end]

cache_type = dedent("""\
typedef struct CompilerDriverTestNativeFrameLinkCacheEntry CompilerDriverTestNativeFrameLinkCacheEntry;
struct CompilerDriverTestNativeFrameLinkCacheEntry
{
    u64 object_hash;
    u64 object_length;
    String8 object_path;
    String8 executable_path;
    u32 fixture;
};

""")

setup_token = "    // Single-lane float vectors retain the established Clang ABI."
if function.count(setup_token) != 1:
    raise SystemExit("native frame vector host setup token changed")
setup = dedent("""\
    enum
    {
        COMPILER_DRIVER_TEST_NATIVE_FRAME_LINK_CACHE_CAPACITY = 4 * 2 * 2 * 3 * 10,
    };
    CompilerDriverTestNativeFrameLinkCacheEntry* native_frame_link_cache = arena_allocate_zeroed(arguments->arena,
        CompilerDriverTestNativeFrameLinkCacheEntry, COMPILER_DRIVER_TEST_NATIVE_FRAME_LINK_CACHE_CAPACITY);
    u32 native_frame_link_cache_count = 0;
    u32 native_frame_link_cache_hits = 0;
    u32 native_frame_link_count = 0;
    u32 native_frame_run_count = 0;
""")
setup = "\n".join("    " + line if line else line for line in setup.splitlines()) + "\n"
function = function.replace(setup_token, setup + setup_token, 1)

condition_token = "if (native_target && executable_cpu && fixture != 3 && compiled.error == COMPILER_DRIVER_ERROR_NONE &&"
condition = function.index(condition_token)
block_start = function.rfind("\n", 0, condition) + 1
runtime_token = "if (link_ok) { BUSTER_TEST(arguments, compiler_driver_test_process_success(temporary.arena, executable)); }"
runtime = function.index(runtime_token, condition)
runtime_line_end = function.index("\n", runtime)
closing_line_start = runtime_line_end + 1
closing_line_end = function.index("\n", closing_line_start)
if function[closing_line_start:closing_line_end].strip() != "}":
    raise SystemExit("native frame vector link block closing brace changed")
block_end = closing_line_end

block = dedent("""\
if (native_target && executable_cpu && fixture != 3 && compiled.error == COMPILER_DRIVER_ERROR_NONE &&
    (observer == UINT32_MAX || host_compiled[observer]))
{
    native_frame_run_count += 1;
    ByteSlice object_bytes = file_read(temporary.arena, object, (FileReadOptions){0});
    u64 object_hash = object_bytes.pointer ? buster_hash_64(object_bytes.pointer, object_bytes.length) : 0;
    CompilerDriverTestNativeFrameLinkCacheEntry* cached = 0;
    if (object_bytes.pointer)
    {
        for (u32 cache_index = 0; cache_index < native_frame_link_cache_count; cache_index += 1)
        {
            CompilerDriverTestNativeFrameLinkCacheEntry* entry = native_frame_link_cache + cache_index;
            if (entry->fixture == fixture && entry->object_hash == object_hash &&
                entry->object_length == object_bytes.length)
            {
                ByteSlice cached_bytes = file_read(temporary.arena, entry->object_path, (FileReadOptions){0});
                if (cached_bytes.pointer && memcmp(cached_bytes.pointer, object_bytes.pointer, object_bytes.length) == 0)
                {
                    cached = entry;
                    break;
                }
            }
        }
    }

    String8 executable = {0};
    bool link_ok = false;
    if (cached)
    {
        executable = cached->executable_path;
        link_ok = true;
        native_frame_link_cache_hits += 1;
    }
    else
    {
        String8 link_object = object;
        String8 retained_object = {0};
        bool retain_object = false;
        if (object_bytes.pointer && native_frame_link_cache_count < COMPILER_DRIVER_TEST_NATIVE_FRAME_LINK_CACHE_CAPACITY)
        {
            String8 object_name = string_format(arguments->arena,
                S8("buster-frame-vector-cache-object-{u32}"), native_frame_link_cache_count);
            String8 executable_name = string_format(arguments->arena,
                S8("buster-frame-vector-cache-run-{u32}"), native_frame_link_cache_count);
            retained_object = buster_test_temporary_path(arguments->arena, object_name, S8(".o"));
            executable = buster_test_temporary_path(arguments->arena, executable_name, S8(".exe"));
            OsError replace_error = os_file_replace(object, retained_object);
            if (!replace_error.v)
            {
                link_object = retained_object;
                retain_object = true;
            }
        }
        if (!executable.length)
        {
            executable = buster_test_temporary_path(temporary.arena, S8("buster-frame-vector-run"), S8(".exe"));
        }

        String8 link_command[10];
        u32 link_count = 0;
        link_command[link_count++] = host_compiler;
        if (configured_clang && S8(BUSTER_HOST_C_COMPILER_ARG1).length) { link_command[link_count++] = S8(BUSTER_HOST_C_COMPILER_ARG1); }
#if BUSTER_LINUX
        link_command[link_count++] = S8("-no-pie");
#endif
        link_command[link_count++] = link_object;
        if (observer != UINT32_MAX) { link_command[link_count++] = host_objects[observer]; }
#if !BUSTER_WINDOWS
        if (fixture == 6) { link_command[link_count++] = S8("-lm"); }
#endif
        link_command[link_count++] = S8("-o");
        link_command[link_count++] = executable;
        native_frame_link_count += 1;
        ProcessSpawnResult linked = os_process_spawn((SliceString8){.pointer = link_command, .length = link_count},
            (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = true});
        link_ok = linked.handle && os_process_wait_sync(temporary.arena, linked).result == PROCESS_RESULT_SUCCESS;
        if (link_ok && retain_object)
        {
            CompilerDriverTestNativeFrameLinkCacheEntry* entry = native_frame_link_cache + native_frame_link_cache_count++;
            entry->object_hash = object_hash;
            entry->object_length = object_bytes.length;
            entry->object_path = retained_object;
            entry->executable_path = executable;
            entry->fixture = fixture;
        }
        else if (retain_object)
        {
            (void)os_file_delete(retained_object);
            (void)os_file_delete(executable);
        }
    }
    BUSTER_TEST(arguments, link_ok);
    if (link_ok) { BUSTER_TEST(arguments, compiler_driver_test_process_success(temporary.arena, executable)); }
}
""").rstrip("\n")
indent = " " * 28
formatted_block = "\n".join(line if line.startswith("#") else indent + line for line in block.splitlines())
function = function[:block_start] + formatted_block + function[block_end:]

return_token = "\n    return result;\n}"
return_position = function.rfind(return_token)
if return_position < 0:
    raise SystemExit("native frame vector return token changed")
cleanup = dedent("""\
#if defined(BUSTER_HOST_C_COMPILER) && !BUSTER_HOST_C_COMPILER_MSVC && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_ANDROID && !BUSTER_IOS
    if (program_flag_get(PROGRAM_FLAG_VERBOSE) && native_frame_run_count)
    {
        arguments->show(arguments,
            S8("NATIVE_FRAME_VECTOR_LINK_CACHE_V1 runs={u32} links={u32} hits={u32} retained={u32}\\n"),
            native_frame_run_count, native_frame_link_count, native_frame_link_cache_hits, native_frame_link_cache_count);
    }
    for (u32 cache_index = 0; cache_index < native_frame_link_cache_count; cache_index += 1)
    {
        (void)os_file_delete(native_frame_link_cache[cache_index].object_path);
        (void)os_file_delete(native_frame_link_cache[cache_index].executable_path);
    }
#endif
""").rstrip("\n")
function = function[:return_position] + "\n" + cleanup + function[return_position:]

updated = text[:function_start] + cache_type + function + text[function_end:]
if updated.count(marker) != 1:
    raise SystemExit("candidate marker count is not one")
path.write_text(updated)
