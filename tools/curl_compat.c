// curl-only opt-in build-system diagnostic (#3181), included by build.c.
// curl_compat_main owns admission and stage evidence; curl_compat_record logs
// compiler/link launcher children. Existing RAD process and pristine guards
// provide bounded capture, cleanup and Git input controls. No upstream patches.
#define CURL_COMPAT_COMMIT "11b991232fbcaa88e2b1faecac224416b0001e35"

BUSTER_GLOBAL_LOCAL bool curl_compat_write(Arena* arena, String8 directory, String8 name, String8 text)
{
    bool result = file_write(path_join(arena, directory, name), BUSTER_SLICE_TO_BYTE_SLICE(text));
    return result;
}

BUSTER_GLOBAL_LOCAL bool curl_compat_step(Arena* arena, String8 directory, String8 evidence, String8 name, SliceString8 arguments, bool* stopped)
{
    bool result = raddebugger_command_ok(raddebugger_command(arena, arguments, directory, path_join(arena, evidence, name),
                                                           RADDEBUGGER_ENVIRONMENT_INHERIT, stopped));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult curl_compat_record(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length >= 3)
    {
        String8 directory = os_path_absolute(arena, arguments.pointer[1], true);
        String8 prefix = path_join(arena, directory, string_format(arena, S8("child-{u64}-{u64}"), os_get_current_process_id(), os_now_microseconds()));
        String8 cwd = os_path_absolute(arena, S8("."), true);
        bool stopped = false;
        bool written = directory.length && cwd.length && file_write(string_format(arena, S8("{S8}.cwd"), prefix), BUSTER_SLICE_TO_BYTE_SLICE(cwd));
        if (written)
        {
            SliceString8 child = {.pointer = arguments.pointer + 2, .length = arguments.length - 2};
            RaddebuggerCommandResult run = raddebugger_command(arena, child, cwd, prefix, RADDEBUGGER_ENVIRONMENT_INHERIT, &stopped);
            ByteSlice output = run.wait.streams[STANDARD_STREAM_OUTPUT];
            ByteSlice error = run.wait.streams[STANDARD_STREAM_ERROR];
            bool emitted = (!output.length || fwrite(output.pointer, 1, (size_t)output.length, stdout) == output.length) &&
                           (!error.length || fwrite(error.pointer, 1, (size_t)error.length, stderr) == error.length);
            result = emitted && raddebugger_command_ok(run) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool curl_compat_configure(Arena* arena, String8 cmake, String8 source, String8 directory,
                                             String8 compiler, bool buster, bool shared, String8 driver, bool* stopped)
{
    String8 records = path_join(arena, directory, S8("children"));
    make_directory_recursive(arena, records);
    String8 launcher = string_format(arena, S8("{S8};test_curl;--record;{S8}"), driver, records);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    String8 flags[] = {
        cmake, S8("-S"), source, S8("-B"), directory, S8("-G"), S8("Ninja"),
        S8("-DCMAKE_BUILD_TYPE=Debug"), S8("-DCMAKE_C_FLAGS=-O0 -g -march=x86-64"),
        S8("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"), S8("-DCMAKE_TRY_COMPILE_NO_PLATFORM_VARIABLES=OFF"),
        S8("-DBUILD_CURL_EXE=ON"), S8("-DBUILD_TESTING=ON"), S8("-DBUILD_EXAMPLES=OFF"),
        S8("-DBUILD_LIBCURL_DOCS=OFF"), S8("-DBUILD_MISC_DOCS=OFF"), S8("-DENABLE_CURL_MANUAL=OFF"),
        S8("-DPICKY_COMPILER=OFF"), S8("-DCURL_WERROR=OFF"), S8("-DHTTP_ONLY=ON"),
        S8("-DCURL_ENABLE_SSL=ON"), S8("-DCURL_USE_OPENSSL=ON"), S8("-DCURL_USE_GNUTLS=OFF"),
        S8("-DCURL_USE_MBEDTLS=OFF"), S8("-DCURL_USE_WOLFSSL=OFF"), S8("-DCURL_USE_RUSTLS=OFF"),
        S8("-DENABLE_ARES=OFF"), S8("-DENABLE_THREADED_RESOLVER=OFF"), S8("-DENABLE_IPV6=ON"),
        S8("-DCURL_ZLIB=OFF"), S8("-DCURL_BROTLI=OFF"), S8("-DCURL_ZSTD=OFF"),
        S8("-DUSE_NGHTTP2=OFF"), S8("-DUSE_NGTCP2=OFF"), S8("-DUSE_QUICHE=OFF"),
        S8("-DUSE_LIBIDN2=OFF"), S8("-DCURL_USE_LIBPSL=OFF"), S8("-DCURL_USE_LIBSSH2=OFF"),
        S8("-DCURL_USE_LIBSSH=OFF"), S8("-DCURL_USE_WOLFSSH=OFF"), S8("-DCURL_USE_GSSAPI=OFF"),
        S8("-DCURL_USE_GSASL=OFF"), S8("-DUSE_LIBRTMP=OFF"), S8("-DCURL_LTO=OFF"),
        S8("-DCURL_USE_PKGCONFIG=OFF"),
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(flags); i += 1)
    {
        os_argument_builder_append(&builder, flags[i]);
    }
    os_argument_builder_append(&builder, string_format(arena, S8("-DCMAKE_C_COMPILER={S8}"), compiler));
    if (buster)
    {
        os_argument_builder_append(&builder, S8("-DCMAKE_C_COMPILER_ARG1=cc"));
    }
    os_argument_builder_append(&builder, string_format(arena, S8("-DCMAKE_C_COMPILER_LAUNCHER={S8}"), launcher));
    os_argument_builder_append(&builder, string_format(arena, S8("-DCMAKE_C_LINKER_LAUNCHER={S8}"), launcher));
    os_argument_builder_append(&builder, shared ? S8("-DBUILD_SHARED_LIBS=ON") : S8("-DBUILD_SHARED_LIBS=OFF"));
    os_argument_builder_append(&builder, shared ? S8("-DBUILD_STATIC_LIBS=OFF") : S8("-DBUILD_STATIC_LIBS=ON"));
    os_argument_builder_append(&builder, shared ? S8("-DBUILD_STATIC_CURL=OFF") : S8("-DBUILD_STATIC_CURL=ON"));
    bool result = curl_compat_step(arena, directory, directory, S8("configure"), os_argument_builder_flush(&builder), stopped);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult curl_compat_census(Arena* arena, SliceString8 arguments, String8 driver)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
    String8 source = os_path_absolute(arena, arguments.pointer[1], true);
    String8 requested = os_path_absolute_lexical(arena, arguments.pointer[2], true);
    String8 parent = os_path_absolute(arena, path_parent(arena, requested), true);
    String8 name = quickjs_basename(requested);
    String8 output = path_join(arena, parent, name);
    String8 ide = os_path_absolute(arena, arguments.pointer[3], true);
    driver = os_path_absolute(arena, driver, true);
    String8 git = executable_resolve_in_path(arena, S8("git"));
    String8 clang = executable_resolve_in_path(arena, S8("clang"));
    String8 cmake = executable_resolve_in_path(arena, S8("cmake"));
    String8 ninja = executable_resolve_in_path(arena, S8("ninja"));
    String8 sha256sum = executable_resolve_in_path(arena, S8("sha256sum"));
    bool stopped = false;
    bool admitted = source.length && parent.length && ide.length && driver.length && git.length && clang.length &&
                    cmake.length && ninja.length && sha256sum.length && clang_suite_safe_path(name) &&
                    !string_equal(parent, source) && !string_starts_with_sequence(parent, string_format(arena, S8("{S8}/"), source)) &&
                    raddebugger_git_verify(arena, git, source, S8(CURL_COMPAT_COMMIT), &stopped);
    if (admitted)
    {
        OsDirectoryCreateResult created = os_make_directory_exclusive(output);
        admitted = created.created;
    }
    if (admitted)
    {
        String8List summary = {0};
        string8_list_push(arena, &summary, S8("compiler\tlibrary\tconfigure\tbuild\tversion\tupstream_tests\n"));
        bool passed = true;
        String8 identity[] = {sha256sum, ide, driver, clang, cmake, ninja, path_join(arena, source, S8("COPYING"))};
        passed = curl_compat_step(arena, output, output, S8("tools-sha256"), (SliceString8)BUSTER_ARRAY_TO_SLICE(identity), &stopped) && passed;
        String8 tree[] = {git, S8("--no-replace-objects"), S8("ls-tree"), S8("-r"), S8("HEAD")};
        passed = raddebugger_command_ok(raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(tree), source,
                                                          path_join(arena, output, S8("source-tree")), RADDEBUGGER_ENVIRONMENT_GIT, &stopped)) && passed;
        String8 dependencies[] = {S8("/usr/bin/pkg-config"), S8("--modversion"), S8("openssl")};
        passed = curl_compat_step(arena, output, output, S8("openssl-version"), (SliceString8)BUSTER_ARRAY_TO_SLICE(dependencies), &stopped) && passed;
        // Run the trusted builds first. A failed Buster stage cannot suppress
        // the reference or turn its detection results into candidate answers.
        for (u32 compiler_i = 0; compiler_i < 2; compiler_i += 1)
        {
            bool buster = compiler_i != 0;
            for (u32 library_i = 0; library_i < 2; library_i += 1)
            {
                bool shared = library_i != 0;
                String8 compiler_name = buster ? S8("buster") : S8("clang");
                String8 library_name = shared ? S8("shared") : S8("static");
                String8 directory = path_join(arena, output, string_format(arena, S8("{S8}-{S8}"), compiler_name, library_name));
                make_directory_recursive(arena, directory);
                bool configured = !stopped && curl_compat_configure(arena, cmake, source, directory, buster ? ide : clang, buster, shared, driver, &stopped);
                bool built = false;
                bool version = false;
                bool tested = false;
                if (configured)
                {
                    String8 build[] = {cmake, S8("--build"), directory, S8("--parallel"), S8("2"), S8("--verbose")};
                    built = curl_compat_step(arena, output, directory, S8("build"), (SliceString8)BUSTER_ARRAY_TO_SLICE(build), &stopped);
                    String8 commands[] = {ninja, S8("-C"), directory, S8("-t"), S8("commands")};
                    String8 deps[] = {ninja, S8("-C"), directory, S8("-t"), S8("deps")};
                    passed = curl_compat_step(arena, output, directory, S8("graph-commands"), (SliceString8)BUSTER_ARRAY_TO_SLICE(commands), &stopped) && passed;
                    passed = curl_compat_step(arena, output, directory, S8("header-dependencies"), (SliceString8)BUSTER_ARRAY_TO_SLICE(deps), &stopped) && passed;
                }
                if (built)
                {
                    String8 binary = path_join(arena, directory, S8("src/curl"));
                    String8 run[] = {binary, S8("--version")};
                    RaddebuggerCommandResult version_run = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(run), directory,
                                                                              path_join(arena, directory, S8("version")), RADDEBUGGER_ENVIRONMENT_HEADLESS, &stopped);
                    version = raddebugger_command_ok(version_run) && string_starts_with_sequence(version_run.output, S8("curl 8.16.0 ")) &&
                              string_first_sequence(version_run.output, S8("OpenSSL/")) != BUSTER_STRING_NO_MATCH &&
                              string_first_sequence(version_run.output, S8("https")) != BUSTER_STRING_NO_MATCH;
                    String8 ctest = executable_resolve_in_path(arena, S8("ctest"));
                    String8 tests[] = {ctest, S8("--test-dir"), directory, S8("--output-on-failure"), S8("--no-tests=error")};
                    tested = ctest.length && curl_compat_step(arena, output, directory, S8("upstream-tests"), (SliceString8)BUSTER_ARRAY_TO_SLICE(tests), &stopped);
                }
                String8 configure_status = configured ? S8("pass") : S8("fail");
                String8 build_status = configured ? (built ? S8("pass") : S8("fail")) : S8("not-run");
                String8 version_status = built ? (version ? S8("pass") : S8("fail")) : S8("not-run");
                String8 test_status = built ? (tested ? S8("pass") : S8("fail")) : S8("not-run");
                string8_list_push(arena, &summary, string_format(arena, S8("{S8}\t{S8}\t{S8}\t{S8}\t{S8}\t{S8}\n"),
                                                               compiler_name, library_name, configure_status, build_status, version_status, test_status));
                string_print(S8("CURL_CELL compiler={S8} library={S8} configure={S8} build={S8} version={S8} tests={S8}\n"),
                             compiler_name, library_name, configure_status, build_status, version_status, test_status);
                passed = configured && built && version && tested && passed;
            }
        }
        bool unchanged = raddebugger_git_verify(arena, git, source, S8(CURL_COMPAT_COMMIT), &stopped);
        String8 text = string_join_arena(arena, string8_list_to_slice(arena, summary), false);
        passed = curl_compat_write(arena, output, S8("summary.tsv"), text) && unchanged && passed;
        bool scope = curl_compat_write(arena, output, S8("scope.txt"),
                                      S8("curl 8.16.0; Linux x86-64; Debug; baseline; synchronous resolver; HTTP/HTTPS; host OpenSSL.\n"
                                         "TLS transfers, mixed ABI, incremental costs and 9700X performance remain unqualified.\n"
                                         "Configure probes not using launchers remain in CMakeConfigureLog.yaml; no fabricated feature answers.\n"));
        string_print(S8("CURL_RESULT commit={S8} pristine_after={u32} status={S8} artifacts={S8}\n"),
                     S8(CURL_COMPAT_COMMIT), (u32)unchanged, passed && scope ? S8("pass") : S8("fail"), output);
        result = passed && scope && !stopped ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else
    {
        string_print(S8("error: test_curl requires pristine pinned curl, built ide, hosted Linux x86-64 tools, and a fresh output outside upstream\n"));
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    BUSTER_UNUSED(driver);
    string_print(S8("error: curl qualification currently requires Linux x86-64\n"));
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult curl_compat_main(Arena* arena, SliceString8 arguments, String8 driver)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test")))
    {
        result = raddebugger_self_test(arena);
    }
    else if (arguments.length >= 3 && string_equal(arguments.pointer[0], S8("--record")))
    {
        result = curl_compat_record(arena, arguments);
    }
    else if (arguments.length == 4 && string_equal(arguments.pointer[0], S8("--census")))
    {
        result = curl_compat_census(arena, arguments, driver);
    }
    else
    {
        string_print(S8("usage: ./build.sh test_curl --self-test | --census /curl /fresh-output /ide\n"));
    }
    return result;
}
