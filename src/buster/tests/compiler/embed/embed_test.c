#include <buster/tests/compiler/embed/embed_test.h>
#include <buster/lib/compiler/embed/embed.h>
#include <buster/lib/system_headers.h>

#if BUSTER_INCLUDE_TESTS
#define EMBED_TEST_EXECUTES ((BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64) && !BUSTER_SANITIZE && !BUSTER_IOS && !BUSTER_ANDROID)

typedef long long EmbedTestHostFunction(long long, long long);
typedef long long EmbedTestRunFunction(long long);

#if EMBED_TEST_EXECUTES
BUSTER_GLOBAL_LOCAL long long embed_test_host_add(long long left, long long right)
{
    return left + right;
}

BUSTER_GLOBAL_LOCAL void embed_test_imports(BusterEmbedImport* imports, long* value)
{
    EmbedTestHostFunction* function = &embed_test_host_add;
    void* address = 0;
    BUSTER_CT_CHECK(sizeof(function) == sizeof(address));
    memcpy(&address, &function, sizeof(address));
    imports[0] = (BusterEmbedImport){.name = {.pointer = "host_add", .length = 8},
                                     .address = address,
                                     .kind = BUSTER_EMBED_SYMBOL_FUNCTION};
    imports[1] = (BusterEmbedImport){.name = {.pointer = "host_scale", .length = 10},
                                     .address = value,
                                     .kind = BUSTER_EMBED_SYMBOL_DATA};
}

BUSTER_GLOBAL_LOCAL bool embed_test_contains(BusterEmbedString string, char const* text)
{
    u64 length = 0;
    while (text[length])
    {
        length += 1;
    }
    bool result = false;
    for (u64 start = 0; start + length <= string.length && !result; start += 1)
    {
        u64 index = 0;
        while (index < length && string.pointer[start + index] == text[index])
        {
            index += 1;
        }
        result = index == length;
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult embed_test_execution(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if EMBED_TEST_EXECUTES
    BusterEmbedCompiler* compiler = 0;
    BUSTER_TEST(arguments, buster_embed_compiler_create(&compiler) == BUSTER_EMBED_STATUS_OK);
    if (compiler)
    {
        char const source[] =
            "extern long long host_add(long long, long long); extern long host_scale; "
            "static long* const host_scale_ref = &host_scale; "
            "long long run(long long x) { return host_add(x, host_scale + *host_scale_ref); }\n";
        BusterEmbedCompileOptions options = {.source_name = {.pointer = "embed_exec.c", .length = 12}};
        long host_scale[2] = {11, 23};
        BusterEmbedImport imports[2][2];
        BusterEmbedModule* modules[2] = {0};
        for (u32 index = 0; index < 2; index += 1)
        {
            embed_test_imports(imports[index], &host_scale[index]);
            BUSTER_TEST(arguments, buster_embed_compile(
                                      compiler, (BusterEmbedString){.pointer = source, .length = sizeof(source) - 1}, &options,
                                      &modules[index]) == BUSTER_EMBED_STATUS_OK);
            void* address = 0;
            BUSTER_TEST(arguments, buster_embed_module_symbol(
                                      modules[index], (BusterEmbedString){"run", 3}, &address) == BUSTER_EMBED_STATUS_NOT_LOADED);
            BusterEmbedStatus load_status = buster_embed_module_load(modules[index], imports[index], 2);
            BUSTER_TEST(arguments, load_status == BUSTER_EMBED_STATUS_OK);
        }
        for (u32 iteration = 0; iteration < 3 && modules[0] && modules[1]; iteration += 1)
        {
            u32 index = iteration & 1;
            void* address = 0;
            BUSTER_TEST(arguments, buster_embed_module_symbol(
                                      modules[index], (BusterEmbedString){"missing", 7}, &address) ==
                                      BUSTER_EMBED_STATUS_SYMBOL_NOT_FOUND);
            BusterEmbedStatus symbol_status = buster_embed_module_symbol(
                modules[index], (BusterEmbedString){"run", 3}, &address);
            BUSTER_TEST(arguments, symbol_status == BUSTER_EMBED_STATUS_OK);
            EmbedTestRunFunction* run = 0;
            BUSTER_CT_CHECK(sizeof(run) == sizeof(address));
            memcpy(&run, &address, sizeof(run));
            if (symbol_status == BUSTER_EMBED_STATUS_OK)
            {
                BUSTER_TEST(arguments, run(7) == 7 + 2 * host_scale[index]);
                host_scale[index] += 3;
                BUSTER_TEST(arguments, run(7) == 7 + 2 * host_scale[index]);
            }
            BUSTER_TEST(arguments, buster_embed_module_load(modules[index], imports[index], 2) == BUSTER_EMBED_STATUS_ALREADY_LOADED);
        }
        buster_embed_module_release(modules[1]);
        buster_embed_module_release(modules[0]);
        BUSTER_TEST(arguments, buster_embed_compiler_destroy(compiler) == BUSTER_EMBED_STATUS_OK);
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult embed_test_unresolved_function(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if EMBED_TEST_EXECUTES
    BusterEmbedCompiler* compiler = 0;
    BUSTER_TEST(arguments, buster_embed_compiler_create(&compiler) == BUSTER_EMBED_STATUS_OK);
    if (compiler)
    {
        char const source[] =
            "extern long long host_add(long long, long long); "
            "long long run(long long x) { return host_add(x, 2); }\n";
        BusterEmbedCompileOptions options = {
            .source_name = {.pointer = "embed_function.c", .length = sizeof("embed_function.c") - 1},
        };
        BusterEmbedModule* module = 0;
        BUSTER_TEST(arguments, buster_embed_compile(
                                  compiler, (BusterEmbedString){source, sizeof(source) - 1}, &options, &module) ==
                                  BUSTER_EMBED_STATUS_OK);
        void* invalid_symbol = 0;
        BUSTER_TEST(arguments, buster_embed_module_symbol(
                                  module, (BusterEmbedString){.pointer = 0, .length = 1}, &invalid_symbol) ==
                                  BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
        EmbedTestHostFunction* function = &embed_test_host_add;
        void* address = 0;
        BUSTER_CT_CHECK(sizeof(function) == sizeof(address));
        memcpy(&address, &function, sizeof(address));
        BusterEmbedImport import = {
            .name = {.pointer = "host_add", .length = 8},
            .address = address,
            .kind = BUSTER_EMBED_SYMBOL_FUNCTION,
        };
        BusterEmbedImport invalid_import = import;
        invalid_import.name = (BusterEmbedString){.pointer = 0, .length = 1};
        BUSTER_TEST(arguments, buster_embed_module_load(module, &invalid_import, 1) == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
        BusterEmbedStatus unresolved = buster_embed_module_load(module, 0, 0);
        BUSTER_TEST(arguments, unresolved == BUSTER_EMBED_STATUS_UNRESOLVED_IMPORT);
        BUSTER_TEST(arguments, embed_test_contains(buster_embed_module_message(module), "host_add"));
        BUSTER_TEST(arguments, buster_embed_module_load(module, &import, 1) == BUSTER_EMBED_STATUS_OK);
        void* symbol = 0;
        BusterEmbedStatus symbol_status = buster_embed_module_symbol(module, (BusterEmbedString){"run", 3}, &symbol);
        BUSTER_TEST(arguments, symbol_status == BUSTER_EMBED_STATUS_OK);
        if (symbol_status == BUSTER_EMBED_STATUS_OK)
        {
            EmbedTestRunFunction* run = 0;
            BUSTER_CT_CHECK(sizeof(run) == sizeof(symbol));
            memcpy(&run, &symbol, sizeof(run));
            BUSTER_TEST(arguments, run(7) == 9);
        }
        buster_embed_module_release(module);
        BUSTER_TEST(arguments, buster_embed_compiler_destroy(compiler) == BUSTER_EMBED_STATUS_OK);
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult embed_test_host_data(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if EMBED_TEST_EXECUTES
    BusterEmbedCompiler* compiler = 0;
    BUSTER_TEST(arguments, buster_embed_compiler_create(&compiler) == BUSTER_EMBED_STATUS_OK);
    if (compiler)
    {
        char const source[] =
            "extern long host_scale; static long* const p = &host_scale; "
            "long run(long x) { host_scale = x; return *p; } "
            "long pointer_write(long x) { *p = x; return host_scale; } "
            "long* address(void) { return &host_scale; }\n";
        BusterEmbedCompileOptions options = {.source_name = {.pointer = "embed_direct.c", .length = 14}};
        BusterEmbedModule* module = 0;
        BUSTER_TEST(arguments, buster_embed_compile(
                                  compiler, (BusterEmbedString){source, sizeof(source) - 1}, &options, &module) ==
                                  BUSTER_EMBED_STATUS_OK);
        long host_scale = 12;
        BusterEmbedImport import = {
            .name = {.pointer = "host_scale", .length = 10},
            .address = &host_scale,
            .kind = BUSTER_EMBED_SYMBOL_DATA,
        };
        BusterEmbedStatus load_status = buster_embed_module_load(module, &import, 1);
        BUSTER_TEST(arguments, load_status == BUSTER_EMBED_STATUS_OK);
        if (load_status == BUSTER_EMBED_STATUS_OK)
        {
            void* address = 0;
            BusterEmbedStatus status = buster_embed_module_symbol(module, (BusterEmbedString){"run", 3}, &address);
            BUSTER_TEST(arguments, status == BUSTER_EMBED_STATUS_OK);
            long (*run)(long) = 0;
            BUSTER_CT_CHECK(sizeof(run) == sizeof(address));
            memcpy(&run, &address, sizeof(run));
            if (status == BUSTER_EMBED_STATUS_OK)
            {
                BUSTER_TEST(arguments, run(31) == 31 && host_scale == 31);
            }
            status = buster_embed_module_symbol(module, (BusterEmbedString){"pointer_write", 13}, &address);
            BUSTER_TEST(arguments, status == BUSTER_EMBED_STATUS_OK);
            memcpy(&run, &address, sizeof(run));
            if (status == BUSTER_EMBED_STATUS_OK)
            {
                BUSTER_TEST(arguments, run(47) == 47 && host_scale == 47);
            }
            status = buster_embed_module_symbol(module, (BusterEmbedString){"address", 7}, &address);
            BUSTER_TEST(arguments, status == BUSTER_EMBED_STATUS_OK);
            long* (*host_address)(void) = 0;
            BUSTER_CT_CHECK(sizeof(host_address) == sizeof(address));
            memcpy(&host_address, &address, sizeof(host_address));
            if (status == BUSTER_EMBED_STATUS_OK)
            {
                BUSTER_TEST(arguments, host_address() == &host_scale);
            }
        }
        buster_embed_module_release(module);
        BUSTER_TEST(arguments, buster_embed_compiler_destroy(compiler) == BUSTER_EMBED_STATUS_OK);
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult embed_test_failures_and_lifetimes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BusterEmbedCompiler* compiler = 0;
    BUSTER_TEST(arguments, buster_embed_compiler_create(0) == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
    BUSTER_TEST(arguments, buster_embed_compiler_destroy(0) == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
    BUSTER_TEST(arguments, buster_embed_module_status(0) == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
    BUSTER_TEST(arguments, buster_embed_module_diagnostic_count(0) == 0);
    BUSTER_TEST(arguments, buster_embed_module_diagnostic(0, 0).code.pointer == 0);
    BUSTER_TEST(arguments, buster_embed_module_message(0).length == 0);
    BusterEmbedBytes null_object = {0};
    BUSTER_TEST(arguments, buster_embed_module_object(0, &null_object) == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
    BUSTER_TEST(arguments, buster_embed_module_load(0, 0, 0) == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
    BUSTER_TEST(arguments, buster_embed_module_symbol(0, (BusterEmbedString){"x", 1}, 0) == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
    BUSTER_TEST(arguments, buster_embed_compiler_create(&compiler) == BUSTER_EMBED_STATUS_OK);
    if (compiler)
    {
        char source[] = "int lifetime_value(void) { return 29; }\n";
        char path[] = "owned_name.c";
        BusterEmbedCompileOptions options = {.source_name = {.pointer = path, .length = sizeof(path) - 1}};
        BusterEmbedModule* module = 0;
        BUSTER_TEST(arguments, buster_embed_compile(
                                  compiler, (BusterEmbedString){.pointer = source, .length = sizeof(source) - 1}, &options, &module) ==
                                  BUSTER_EMBED_STATUS_OK);
        memset(source, 'x', sizeof(source) - 1);
        memset(path, 'y', sizeof(path) - 1);
        if (module)
        {
            BusterEmbedBytes object = {0};
            BUSTER_TEST(arguments, buster_embed_module_object(module, &object) == BUSTER_EMBED_STATUS_OK);
            BUSTER_TEST(arguments, object.length != 0);
#if EMBED_TEST_EXECUTES
            BUSTER_TEST(arguments, buster_embed_module_load(module, 0, 0) == BUSTER_EMBED_STATUS_OK);
            void* address = 0;
            BusterEmbedStatus symbol_status = buster_embed_module_symbol(
                module, (BusterEmbedString){"lifetime_value", 14}, &address);
            BUSTER_TEST(arguments, symbol_status == BUSTER_EMBED_STATUS_OK);
            EmbedTestRunFunction* function = 0;
            BUSTER_CT_CHECK(sizeof(function) == sizeof(address));
            memcpy(&function, &address, sizeof(function));
            if (symbol_status == BUSTER_EMBED_STATUS_OK)
            {
                BUSTER_TEST(arguments, function(0) == 29);
            }
#endif
            buster_embed_module_release(module);
        }
        BusterEmbedCompileOptions invalid_options = {.source_name = {.pointer = "broken.c", .length = 8}};
        BusterEmbedModule* invalid = 0;
        BUSTER_TEST(arguments, buster_embed_compile(
                                  compiler, (BusterEmbedString){"int broken( {", 13}, &invalid_options, &invalid) ==
                                  BUSTER_EMBED_STATUS_COMPILE_FAILED);
        if (invalid)
        {
            BUSTER_TEST(arguments, buster_embed_module_diagnostic_count(invalid) != 0);
            BusterEmbedDiagnostic diagnostic = buster_embed_module_diagnostic(invalid, 0);
            BUSTER_TEST(arguments, diagnostic.severity == BUSTER_EMBED_SEVERITY_ERROR);
            BUSTER_TEST(arguments, diagnostic.code.pointer && diagnostic.message.pointer);
            BUSTER_TEST(arguments, diagnostic.path.length == 8 && diagnostic.line >= 1);
            BUSTER_TEST(arguments, buster_embed_module_load(invalid, 0, 0) == BUSTER_EMBED_STATUS_NOT_COMPILED);
            BusterEmbedBytes object = {0};
            BUSTER_TEST(arguments, buster_embed_module_object(invalid, &object) == BUSTER_EMBED_STATUS_NOT_COMPILED);
            BUSTER_TEST(arguments, buster_embed_compiler_destroy(compiler) == BUSTER_EMBED_STATUS_BUSY);
            buster_embed_module_release(invalid);
        }
        BusterEmbedString invalid_string = {.pointer = 0, .length = 1};
        char const valid_source[] = "int valid(void) { return 1; }\n";
        BusterEmbedString invalid_include_paths[] = {invalid_string};
        BusterEmbedString invalid_definitions[] = {invalid_string};
        BusterEmbedString invalid_arguments[] = {invalid_string};
        BusterEmbedCompileOptions invalid_compile_options[] = {
            {.source_name = invalid_string},
            {.target = invalid_string},
            {.dialect = invalid_string},
            {.include_paths = invalid_include_paths, .include_path_count = 1},
            {.include_path_count = 1},
            {.definitions = invalid_definitions, .definition_count = 1},
            {.definition_count = 1},
            {.arguments = invalid_arguments, .argument_count = 1},
            {.argument_count = 1},
        };
        BusterEmbedModule* invalid_source_module = 0;
        BUSTER_TEST(arguments, buster_embed_compile(
                                  compiler, invalid_string, 0, &invalid_source_module) == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
        BUSTER_TEST(arguments, invalid_source_module != 0);
        BUSTER_TEST(arguments, invalid_source_module && buster_embed_module_message(invalid_source_module).length != 0);
        BUSTER_TEST(arguments, invalid_source_module && buster_embed_module_diagnostic_count(invalid_source_module) != 0);
        buster_embed_module_release(invalid_source_module);
        u32 invalid_option_count = (u32)(sizeof(invalid_compile_options) / sizeof(invalid_compile_options[0]));
        for (u32 index = 0; index < invalid_option_count; index += 1)
        {
            BusterEmbedModule* invalid_module = 0;
            BusterEmbedStatus invalid_status = buster_embed_compile(
                compiler, (BusterEmbedString){.pointer = valid_source, .length = sizeof(valid_source) - 1},
                &invalid_compile_options[index], &invalid_module);
            BUSTER_TEST(arguments, invalid_status == BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
            BUSTER_TEST(arguments, invalid_module != 0);
            BUSTER_TEST(arguments, invalid_module && buster_embed_module_message(invalid_module).length != 0);
            BUSTER_TEST(arguments, invalid_module && buster_embed_module_diagnostic_count(invalid_module) != 0);
            if (!index && invalid_module)
            {
                BusterEmbedDiagnostic diagnostic = buster_embed_module_diagnostic(invalid_module, 0);
                BUSTER_TEST(arguments, diagnostic.path.pointer && diagnostic.path.length == 7 &&
                                           memcmp(diagnostic.path.pointer, "input.c", 7) == 0);
            }
            buster_embed_module_release(invalid_module);
        }
        BusterEmbedString bad_action = {.pointer = "-E", .length = 2};
        BusterEmbedCompileOptions bad_options = {.arguments = &bad_action, .argument_count = 1};
        BusterEmbedModule* bad_module = 0;
        BUSTER_TEST(arguments, buster_embed_compile(
                                  compiler, (BusterEmbedString){"int f(void) { return 1; }", 25}, &bad_options, &bad_module) ==
                                  BUSTER_EMBED_STATUS_INVALID_ARGUMENT);
        BUSTER_TEST(arguments, buster_embed_module_message(bad_module).length != 0);
        buster_embed_module_release(bad_module);
        BUSTER_TEST(arguments, buster_embed_compiler_destroy(compiler) == BUSTER_EMBED_STATUS_OK);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult embed_test_foreign_target(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if defined(__linux__) && (BUSTER_CPU_ARCH_X86_64 || BUSTER_CPU_ARCH_AARCH64)
    BusterEmbedCompiler* compiler = 0;
    BUSTER_TEST(arguments, buster_embed_compiler_create(&compiler) == BUSTER_EMBED_STATUS_OK);
    if (compiler)
    {
#if BUSTER_CPU_ARCH_X86_64
        char const target_name[] = "aarch64-unknown-linux-gnu";
#else
        char const target_name[] = "x86_64-unknown-linux-gnu";
#endif
        char const foreign_source[] = "int foreign(void) { return 3; }";
        BusterEmbedString target = {.pointer = target_name, .length = sizeof(target_name) - 1};
        char const foreign_name[] = "foreign.c";
        BusterEmbedCompileOptions options = {
            .target = target,
            .source_name = {.pointer = foreign_name, .length = sizeof(foreign_name) - 1},
        };
        BusterEmbedModule* module = 0;
        BUSTER_TEST(arguments, buster_embed_compile(
                                  compiler, (BusterEmbedString){foreign_source, sizeof(foreign_source) - 1}, &options, &module) ==
                                  BUSTER_EMBED_STATUS_OK);
        if (module)
        {
            BusterEmbedBytes object = {0};
            BUSTER_TEST(arguments, buster_embed_module_object(module, &object) == BUSTER_EMBED_STATUS_OK);
            BUSTER_TEST(arguments, object.length >= 4 && object.pointer[0] == 0x7f && object.pointer[1] == 'E' &&
                                       object.pointer[2] == 'L' && object.pointer[3] == 'F');
            BUSTER_TEST(arguments, buster_embed_module_load(module, 0, 0) == BUSTER_EMBED_STATUS_FOREIGN_TARGET);
            buster_embed_module_release(module);
        }
        BUSTER_TEST(arguments, buster_embed_compiler_destroy(compiler) == BUSTER_EMBED_STATUS_OK);
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

UnitTestResult embed_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    UnitTestResult execution = embed_test_execution(arguments);
    result.succeeded_test_count += execution.succeeded_test_count;
    result.test_count += execution.test_count;
    UnitTestResult failures = embed_test_failures_and_lifetimes(arguments);
    result.succeeded_test_count += failures.succeeded_test_count;
    result.test_count += failures.test_count;
    UnitTestResult unresolved = embed_test_unresolved_function(arguments);
    result.succeeded_test_count += unresolved.succeeded_test_count;
    result.test_count += unresolved.test_count;
    UnitTestResult direct_data = embed_test_host_data(arguments);
    result.succeeded_test_count += direct_data.succeeded_test_count;
    result.test_count += direct_data.test_count;
    UnitTestResult foreign = embed_test_foreign_target(arguments);
    result.succeeded_test_count += foreign.succeeded_test_count;
    result.test_count += foreign.test_count;
    return result;
}
#endif
