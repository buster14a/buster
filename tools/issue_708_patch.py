from pathlib import Path

path = Path("src/buster/tests/compiler/driver/driver_test.c")
text = path.read_text()
marker = "NATIVE_FRAME_VECTOR_LINK_CACHE_V1"
if marker in text:
    raise SystemExit(0)

function_marker = "BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_native_frame_vectors(UnitTestArguments* arguments)\n"
next_marker = "\nBUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_sysv_sseup(UnitTestArguments* arguments)"
function_start = text.index(function_marker)
function_end = text.index(next_marker, function_start)
function = text[function_start:function_end]

cache_type = r'''typedef struct CompilerDriverTestNativeFrameLinkCacheEntry CompilerDriverTestNativeFrameLinkCacheEntry;
struct CompilerDriverTestNativeFrameLinkCacheEntry
{
    u64 object_hash;
    u64 object_length;
    String8 object_path;
    String8 executable_path;
    u32 fixture;
    u32 reserved;
};

'''

setup_anchor = '''#if defined(BUSTER_HOST_C_COMPILER) && !BUSTER_HOST_C_COMPILER_MSVC && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_ANDROID && !BUSTER_IOS
    // Single-lane float vectors retain the established Clang ABI.'''
setup_replacement = '''#if defined(BUSTER_HOST_C_COMPILER) && !BUSTER_HOST_C_COMPILER_MSVC && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_ANDROID && !BUSTER_IOS
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
    // Single-lane float vectors retain the established Clang ABI.'''
if function.count(setup_anchor) != 1:
    raise SystemExit("native frame vector host setup anchor changed")
function = function.replace(setup_anchor, setup_replacement, 1)

block_start_text = '''                            if (native_target && executable_cpu && fixture != 3 && compiled.error == COMPILER_DRIVER_ERROR_NONE &&
                                (observer == UINT32_MAX || host_compiled[observer]))
                    {'''
block_tail = '''                        BUSTER_TEST(arguments, link_ok);
                        if (link_ok) { BUSTER_TEST(arguments, compiler_driver_test_process_success(temporary.arena, executable)); }
                    }'''
block_start = function.index(block_start_text)
block_end = function.index(block_tail, block_start) + len(block_tail)
replacement = r'''                            if (native_target && executable_cpu && fixture != 3 && compiled.error == COMPILER_DRIVER_ERROR_NONE &&
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
                            if (object_bytes.pointer &&
                                native_frame_link_cache_count < COMPILER_DRIVER_TEST_NATIVE_FRAME_LINK_CACHE_CAPACITY)
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
                                CompilerDriverTestNativeFrameLinkCacheEntry* entry =
                                    native_frame_link_cache + native_frame_link_cache_count++;
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
                    }'''
function = function[:block_start] + replacement + function[block_end:]

return_anchor = "\n    return result;\n}"
return_index = function.rfind(return_anchor)
if return_index < 0:
    raise SystemExit("native frame vector return anchor changed")
cleanup = r'''
#if defined(BUSTER_HOST_C_COMPILER) && !BUSTER_HOST_C_COMPILER_MSVC && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_ANDROID && !BUSTER_IOS
    if (program_flag_get(PROGRAM_FLAG_VERBOSE) && native_frame_run_count)
    {
        arguments->show(arguments,
            S8("NATIVE_FRAME_VECTOR_LINK_CACHE_V1 runs={u32} links={u32} hits={u32} retained={u32}\n"),
            native_frame_run_count, native_frame_link_count, native_frame_link_cache_hits, native_frame_link_cache_count);
    }
    for (u32 cache_index = 0; cache_index < native_frame_link_cache_count; cache_index += 1)
    {
        (void)os_file_delete(native_frame_link_cache[cache_index].object_path);
        (void)os_file_delete(native_frame_link_cache[cache_index].executable_path);
    }
#endif'''
function = function[:return_index] + cleanup + function[return_index:]

updated = text[:function_start] + cache_type + function + text[function_end:]
if updated.count(marker) != 1:
    raise SystemExit("candidate marker count is not one")
path.write_text(updated)
