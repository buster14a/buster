// Embedding facade over the compiler driver, native object writer and object
// JIT. The public entry points below own no caller buffers: a module arena
// retains every source, option-derived name, diagnostic and JIT binding name
// until the module is released.

#include <buster/lib/compiler/embed/embed.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/target.h>

#ifndef BUSTER_EMBED_STANDALONE_RUNTIME
#define BUSTER_EMBED_STANDALONE_RUNTIME 0
#endif

#if BUSTER_EMBED_STANDALONE_RUNTIME
BUSTER_V_IMPL OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState compiler_embed_program_state;
BUSTER_V_IMPL ProgramState* program_state = &compiler_embed_program_state;
#endif

typedef struct BusterEmbedCompiler BusterEmbedCompiler;
struct BusterEmbedCompiler
{
    Arena* arena;
    ThreadContext* thread_context;
    BusterEmbedCompiler* next;
    u32 live_module_count;
    bool owns_thread_context;
};

typedef struct BusterEmbedModule BusterEmbedModule;
struct BusterEmbedModule
{
    BusterEmbedCompiler* compiler;
    Arena* arena;
    CompilerDriverResult result;
    BusterEmbedStatus status;
    BusterEmbedDiagnostic* diagnostics;
    u32 diagnostic_count;
    BusterEmbedString source_name;
    BusterEmbedString message;
    ObjectArtifact object_artifact;
    JitProgram jit;
    bool object_ready;
    bool loaded;
};

BUSTER_GLOBAL_LOCAL BusterEmbedCompiler* compiler_embed_compilers;

BUSTER_GLOBAL_LOCAL BusterEmbedString compiler_embed_empty_string(void)
{
    BusterEmbedString result = {0};
    return result;
}

BUSTER_GLOBAL_LOCAL BusterEmbedString compiler_embed_public_string(String8 string)
{
    BusterEmbedString result = {.pointer = (char const*)string.pointer, .length = (size_t)string.length};
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_embed_internal_string(BusterEmbedString string)
{
    String8 result = {.pointer = (char8*)string.pointer, .length = (u64)string.length};
    return result;
}

BUSTER_GLOBAL_LOCAL BusterEmbedString compiler_embed_copy_string(BusterEmbedModule* module, String8 string)
{
    String8 copy = string_duplicate_arena(module->arena, string, true);
    BusterEmbedString result = compiler_embed_public_string(copy);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_embed_string_valid(BusterEmbedString string)
{
    bool result = string.pointer || !string.length;
    for (u64 index = 0; index < (u64)string.length && result; index += 1)
    {
        result = string.pointer[index] != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_embed_set_message(BusterEmbedModule* module, String8 message)
{
    module->message = compiler_embed_copy_string(module, message);
}

BUSTER_GLOBAL_LOCAL String8 compiler_embed_driver_error_name(CompilerDriverError error)
{
    String8 result;
    switch (error)
    {
        case COMPILER_DRIVER_ERROR_NONE:
            result = S8("none");
            break;
        case COMPILER_DRIVER_ERROR_ARGUMENT:
            result = S8("argument");
            break;
        case COMPILER_DRIVER_ERROR_INVALID_INPUT:
            result = S8("invalid-input");
            break;
        case COMPILER_DRIVER_ERROR_FILE_READ:
            result = S8("file-read");
            break;
        case COMPILER_DRIVER_ERROR_TOKENIZE:
            result = S8("tokenize");
            break;
        case COMPILER_DRIVER_ERROR_PARSE:
            result = S8("parse");
            break;
        case COMPILER_DRIVER_ERROR_ANALYSIS:
            result = S8("analysis");
            break;
        case COMPILER_DRIVER_ERROR_IR:
            result = S8("ir");
            break;
        case COMPILER_DRIVER_ERROR_LLVM_BITCODE:
            result = S8("llvm-bitcode");
            break;
        case COMPILER_DRIVER_ERROR_CODEGEN:
            result = S8("codegen");
            break;
        case COMPILER_DRIVER_ERROR_WASM64:
            result = S8("wasm");
            break;
        case COMPILER_DRIVER_ERROR_GPU:
            result = S8("gpu");
            break;
        case COMPILER_DRIVER_ERROR_EBPF:
            result = S8("ebpf");
            break;
        case COMPILER_DRIVER_ERROR_OBJECT:
            result = S8("object");
            break;
        case COMPILER_DRIVER_ERROR_LINK:
            result = S8("link");
            break;
        case COMPILER_DRIVER_ERROR_FILE_WRITE:
            result = S8("file-write");
            break;
        case COMPILER_DRIVER_ERROR_SPIRV:
            result = S8("spirv");
            break;
        case COMPILER_DRIVER_ERROR_RESOURCE:
            result = S8("resource");
            break;
        case COMPILER_DRIVER_ERROR_COUNT:
            result = S8("unknown");
            break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BusterEmbedStatus compiler_embed_driver_status(CompilerDriverError error)
{
    BusterEmbedStatus result;
    switch (error)
    {
        case COMPILER_DRIVER_ERROR_NONE:
            result = BUSTER_EMBED_STATUS_OK;
            break;
        case COMPILER_DRIVER_ERROR_ARGUMENT:
            result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
            break;
        case COMPILER_DRIVER_ERROR_RESOURCE:
            result = BUSTER_EMBED_STATUS_RESOURCE;
            break;
        default:
            result = BUSTER_EMBED_STATUS_COMPILE_FAILED;
            break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BusterEmbedStatus compiler_embed_jit_status(JitError error)
{
    BusterEmbedStatus result;
    switch (error)
    {
        case JIT_ERROR_NONE:
            result = BUSTER_EMBED_STATUS_OK;
            break;
        case JIT_ERROR_FOREIGN_TARGET:
            result = BUSTER_EMBED_STATUS_FOREIGN_TARGET;
            break;
        case JIT_ERROR_UNRESOLVED_IMPORT:
            result = BUSTER_EMBED_STATUS_UNRESOLVED_IMPORT;
            break;
        default:
            result = BUSTER_EMBED_STATUS_JIT_FAILED;
            break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BusterEmbedSeverity compiler_embed_severity(CompilerDiagnosticSeverity severity)
{
    BusterEmbedSeverity result;
    switch (severity)
    {
        case COMPILER_DIAGNOSTIC_ERROR:
            result = BUSTER_EMBED_SEVERITY_ERROR;
            break;
        case COMPILER_DIAGNOSTIC_WARNING:
            result = BUSTER_EMBED_SEVERITY_WARNING;
            break;
        case COMPILER_DIAGNOSTIC_NOTE:
            result = BUSTER_EMBED_SEVERITY_NOTE;
            break;
        default:
            result = BUSTER_EMBED_SEVERITY_ERROR;
            break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_embed_synthesize_diagnostic(BusterEmbedModule* module)
{
    module->diagnostics = arena_allocate(module->arena, BusterEmbedDiagnostic, 1);
    module->diagnostic_count = 1;
    BusterEmbedDiagnostic* diagnostic = module->diagnostics;
    String8 code = string_format(module->arena, S8("driver.{S8}"), compiler_embed_driver_error_name(module->result.error));
    String8 message = module->result.diagnostic.length ? module->result.diagnostic : S8("compiler driver failed");
    diagnostic->code = compiler_embed_copy_string(module, code);
    diagnostic->message = compiler_embed_copy_string(module, message);
    diagnostic->path = module->source_name;
    diagnostic->rendered = diagnostic->message;
    diagnostic->severity = BUSTER_EMBED_SEVERITY_ERROR;
}

BUSTER_GLOBAL_LOCAL void compiler_embed_copy_diagnostics(BusterEmbedModule* module)
{
    module->diagnostic_count = module->result.diagnostic_count;
    if (module->diagnostic_count)
    {
        module->diagnostics = arena_allocate(module->arena, BusterEmbedDiagnostic, module->diagnostic_count);
        for (u32 index = 0; index < module->diagnostic_count; index += 1)
        {
            CompilerDiagnostic const* source = module->result.diagnostics + index;
            BusterEmbedDiagnostic* destination = module->diagnostics + index;
            destination->code = compiler_embed_copy_string(module, source->code);
            destination->message = compiler_embed_copy_string(module, source->message);
            String8 path = source->primary.path.length ? source->primary.path : compiler_embed_internal_string(module->source_name);
            destination->path = compiler_embed_copy_string(module, path);
            destination->rendered = compiler_embed_copy_string(module, compiler_diagnostic_render(module->arena, *source));
            destination->line = source->primary.position.line;
            destination->column = source->primary.position.column;
            destination->severity = compiler_embed_severity(source->severity);
        }
    }
    else if (module->status != BUSTER_EMBED_STATUS_OK)
    {
        compiler_embed_synthesize_diagnostic(module);
    }
}

BUSTER_GLOBAL_LOCAL void compiler_embed_set_driver_failure(BusterEmbedModule* module, CompilerDriverError error, String8 message)
{
    module->result.error = error;
    module->result.diagnostic = message;
    module->status = compiler_embed_driver_status(error);
    compiler_embed_set_message(module, message);
    compiler_embed_copy_diagnostics(module);
}

#if BUSTER_EMBED_STANDALONE_RUNTIME
BUSTER_GLOBAL_LOCAL BusterEmbedStatus compiler_embed_runtime_initialize(void)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_OK;
    if (!os_state.arena)
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        os_state.arena = arena_create((ArenaCreation){0});
        if (!os_state.arena)
        {
            result = BUSTER_EMBED_STATUS_RESOURCE;
        }
        else
        {
            os_state.entity_arena = arena_create((ArenaCreation){0});
            if (!os_state.entity_arena)
            {
                arena_destroy(os_state.arena, 1);
                os_state.arena = 0;
                result = BUSTER_EMBED_STATUS_RESOURCE;
            }
            else
            {
#if defined(__linux__) || defined(__APPLE__)
                pthread_mutex_init(&os_state.entity_mutex, 0);
#elif defined(_WIN32)
                InitializeCriticalSection(&os_state.entity_mutex);
                LARGE_INTEGER frequency;
                os_state.frequency = QueryPerformanceFrequency(&frequency) ? (u64)frequency.QuadPart : 0;
#endif
                os_state.logical_thread_count = os_get_logical_thread_count();
                os_state.page_size = os_get_page_size();
                os_state.large_page_size = BUSTER_MB(2);
                os_state.allocation_granularity = os_state.page_size;
                os_state.process.handle = os_get_current_process_handle();
                cpu_detect_model();
                program_state->arena = arena_create((ArenaCreation){0});
                if (!program_state->arena)
                {
#if defined(__linux__) || defined(__APPLE__)
                    pthread_mutex_destroy(&os_state.entity_mutex);
#elif defined(_WIN32)
                    DeleteCriticalSection(&os_state.entity_mutex);
#endif
                    arena_destroy(os_state.entity_arena, 1);
                    arena_destroy(os_state.arena, 1);
                    os_state = (OsState){0};
                    result = BUSTER_EMBED_STATUS_RESOURCE;
                }
#if defined(_WIN32)
                else
                {
                    char16 const* environment_block = (char16 const*)GetEnvironmentStringsW();
                    SliceString8 raw_environment = string16_environment_block_to_slice_string(os_state.arena, environment_block);
                    if (environment_block)
                    {
                        FreeEnvironmentStringsW((LPWCH)environment_block);
                    }
                    if (raw_environment.length)
                    {
                        String8* keys = arena_allocate(os_state.arena, String8, raw_environment.length);
                        String8* values = arena_allocate(os_state.arena, String8, raw_environment.length);
                        for (u64 index = 0; index < raw_environment.length; index += 1)
                        {
                            u64 equal = string_first_code_unit(raw_environment.pointer[index], '=');
                            if (equal != BUSTER_STRING_NO_MATCH && equal)
                            {
                                keys[program_state->input.environment_keys.length] =
                                    string_slice(raw_environment.pointer[index], 0, equal);
                                values[program_state->input.environment_values.length] =
                                    string_slice(raw_environment.pointer[index], equal + 1, raw_environment.pointer[index].length);
                                program_state->input.environment_keys.length += 1;
                                program_state->input.environment_values.length += 1;
                            }
                        }
                        program_state->input.environment_keys.pointer = keys;
                        program_state->input.environment_values.pointer = values;
                    }
                }
#endif
            }
        }
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL BusterEmbedStatus compiler_embed_prepare_runtime(void)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_OK;
#if BUSTER_EMBED_STANDALONE_RUNTIME
    result = compiler_embed_runtime_initialize();
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_embed_remove_compiler(BusterEmbedCompiler* compiler)
{
    BusterEmbedCompiler** link = &compiler_embed_compilers;
    while (*link && *link != compiler)
    {
        link = &(*link)->next;
    }
    if (*link == compiler)
    {
        *link = compiler->next;
    }
}

BUSTER_GLOBAL_LOCAL void compiler_embed_transfer_context(BusterEmbedCompiler* compiler)
{
    bool released = false;
    if (compiler->owns_thread_context)
    {
        BusterEmbedCompiler* replacement = compiler_embed_compilers;
        while (replacement && (replacement == compiler || replacement->thread_context != compiler->thread_context))
        {
            replacement = replacement->next;
        }
        if (replacement)
        {
            replacement->owns_thread_context = true;
        }
        else
        {
            ThreadContext* current = thread_context_selected();
            if (current != compiler->thread_context)
            {
                thread_context_select(compiler->thread_context);
            }
            thread_context_release(compiler->thread_context);
            if (current && current != compiler->thread_context)
            {
                thread_context_select(current);
            }
            released = true;
        }
    }
    BUSTER_UNUSED(released);
}

BUSTER_GLOBAL_LOCAL BusterEmbedModule* compiler_embed_module_allocate(BusterEmbedCompiler* compiler)
{
    BusterEmbedModule* result = 0;
    Arena* arena = arena_create((ArenaCreation){.reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE});
    if (arena)
    {
        result = arena_allocate(arena, BusterEmbedModule, 1);
        memset(result, 0, sizeof(*result));
        result->compiler = compiler;
        result->arena = arena;
        result->status = BUSTER_EMBED_STATUS_COMPILE_FAILED;
        compiler->live_module_count += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_embed_append_argument(Arena* arena, String8* arguments, u64* count, String8 value)
{
    bool result = *count < UINT32_MAX;
    if (result)
    {
        arguments[*count] = string_duplicate_arena(arena, value, true);
        *count += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_embed_append_option(Arena* arena, String8* arguments, u64* count, String8 prefix, BusterEmbedString value)
{
    bool result = compiler_embed_string_valid(value) && value.length != 0;
    if (result)
    {
        result = compiler_embed_append_argument(arena, arguments, count, string_format(arena, S8("{S8}{S8}"), prefix, compiler_embed_internal_string(value)));
    }
    return result;
}

uint32_t buster_embed_api_version(void)
{
    return BUSTER_EMBED_API_VERSION;
}

BusterEmbedString buster_embed_status_string(BusterEmbedStatus status)
{
    BusterEmbedString result = compiler_embed_empty_string();
    switch (status)
    {
        case BUSTER_EMBED_STATUS_OK:
            result = compiler_embed_public_string(S8("ok"));
            break;
        case BUSTER_EMBED_STATUS_INVALID_ARGUMENT:
            result = compiler_embed_public_string(S8("invalid argument"));
            break;
        case BUSTER_EMBED_STATUS_RESOURCE:
            result = compiler_embed_public_string(S8("resource failure"));
            break;
        case BUSTER_EMBED_STATUS_COMPILE_FAILED:
            result = compiler_embed_public_string(S8("compile failed"));
            break;
        case BUSTER_EMBED_STATUS_NOT_COMPILED:
            result = compiler_embed_public_string(S8("module not compiled"));
            break;
        case BUSTER_EMBED_STATUS_ALREADY_LOADED:
            result = compiler_embed_public_string(S8("module already loaded"));
            break;
        case BUSTER_EMBED_STATUS_NOT_LOADED:
            result = compiler_embed_public_string(S8("module not loaded"));
            break;
        case BUSTER_EMBED_STATUS_FOREIGN_TARGET:
            result = compiler_embed_public_string(S8("foreign target"));
            break;
        case BUSTER_EMBED_STATUS_UNRESOLVED_IMPORT:
            result = compiler_embed_public_string(S8("unresolved import"));
            break;
        case BUSTER_EMBED_STATUS_JIT_FAILED:
            result = compiler_embed_public_string(S8("JIT failed"));
            break;
        case BUSTER_EMBED_STATUS_SYMBOL_NOT_FOUND:
            result = compiler_embed_public_string(S8("symbol not found"));
            break;
        case BUSTER_EMBED_STATUS_BUSY:
            result = compiler_embed_public_string(S8("busy"));
            break;
        case BUSTER_EMBED_STATUS_COUNT:
            break;
    }
    return result;
}

BusterEmbedStatus buster_embed_compiler_create(BusterEmbedCompiler** out_compiler)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
    if (out_compiler)
    {
        *out_compiler = 0;
        result = compiler_embed_prepare_runtime();
        if (result == BUSTER_EMBED_STATUS_OK)
        {
            ThreadContext* thread_context = thread_context_selected();
            bool owns_thread_context = false;
            if (!thread_context)
            {
                thread_context = thread_context_allocate();
                if (thread_context)
                {
                    thread_context_select(thread_context);
                    owns_thread_context = true;
                }
                else
                {
                    result = BUSTER_EMBED_STATUS_RESOURCE;
                }
            }
            if (result == BUSTER_EMBED_STATUS_OK)
            {
                compiler_prewarm();
                Arena* arena = arena_create((ArenaCreation){0});
                if (!arena)
                {
                    if (owns_thread_context)
                    {
                        thread_context_release(thread_context);
                    }
                    result = BUSTER_EMBED_STATUS_RESOURCE;
                }
                else
                {
                    BusterEmbedCompiler* compiler = arena_allocate(arena, BusterEmbedCompiler, 1);
                    memset(compiler, 0, sizeof(*compiler));
                    compiler->arena = arena;
                    compiler->thread_context = thread_context;
                    compiler->owns_thread_context = owns_thread_context;
                    compiler->next = compiler_embed_compilers;
                    compiler_embed_compilers = compiler;
                    *out_compiler = compiler;
                }
            }
        }
    }
    return result;
}

BusterEmbedStatus buster_embed_compiler_destroy(BusterEmbedCompiler* compiler)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
    if (compiler)
    {
        result = compiler->live_module_count ? BUSTER_EMBED_STATUS_BUSY : BUSTER_EMBED_STATUS_OK;
        if (result == BUSTER_EMBED_STATUS_OK)
        {
            compiler_embed_remove_compiler(compiler);
            compiler_embed_transfer_context(compiler);
            arena_destroy(compiler->arena, 1);
        }
    }
    return result;
}

BusterEmbedStatus buster_embed_compile(
    BusterEmbedCompiler* compiler,
    BusterEmbedString source,
    BusterEmbedCompileOptions const* options,
    BusterEmbedModule** out_module)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
    if (out_module)
    {
        *out_module = 0;
        if (compiler)
        {
            BusterEmbedModule* module = compiler_embed_module_allocate(compiler);
            if (module)
            {
                *out_module = module;
                BusterEmbedString requested_source_name = options ? options->source_name : compiler_embed_empty_string();
                bool valid = compiler_embed_string_valid(source) && compiler_embed_string_valid(requested_source_name);
                u64 argument_count = 3;
                if (valid && options)
                {
                    valid = compiler_embed_string_valid(options->target) && compiler_embed_string_valid(options->dialect) &&
                            (!options->include_path_count || options->include_paths) &&
                            (!options->definition_count || options->definitions) &&
                            (!options->argument_count || options->arguments);
                }
                if (valid && options)
                {
                    argument_count += options->target.length != 0;
                    argument_count += options->dialect.length != 0;
                    argument_count += options->include_path_count;
                    argument_count += options->definition_count;
                    argument_count += options->argument_count;
                    valid = argument_count <= UINT32_MAX;
                }
                for (u32 index = 0; valid && options && index < options->include_path_count; index += 1)
                {
                    BusterEmbedString include_path = options->include_paths[index];
                    valid = compiler_embed_string_valid(include_path) && include_path.length != 0;
                }
                for (u32 index = 0; valid && options && index < options->definition_count; index += 1)
                {
                    BusterEmbedString definition = options->definitions[index];
                    valid = compiler_embed_string_valid(definition) && definition.length != 0;
                }
                for (u32 index = 0; valid && options && index < options->argument_count; index += 1)
                {
                    valid = compiler_embed_string_valid(options->arguments[index]);
                }
                BusterEmbedString source_name = {.pointer = "input.c", .length = 7};
                if (valid && requested_source_name.length)
                {
                    source_name = requested_source_name;
                }
                module->source_name = compiler_embed_copy_string(module, compiler_embed_internal_string(source_name));
                String8* arguments = valid ? arena_allocate(module->arena, String8, argument_count) : 0;
                u64 count = 0;
                if (valid && arguments)
                {
                    valid = compiler_embed_append_argument(module->arena, arguments, &count, S8("-c"));
                    if (options && options->target.length)
                    {
                        valid = valid && compiler_embed_append_option(module->arena, arguments, &count, S8("--target="), options->target);
                    }
                    if (options && options->dialect.length)
                    {
                        valid = valid && compiler_embed_append_option(module->arena, arguments, &count, S8("-std="), options->dialect);
                    }
                    for (u32 index = 0; options && index < options->include_path_count && valid; index += 1)
                    {
                        valid = compiler_embed_append_option(module->arena, arguments, &count, S8("-I"), options->include_paths[index]);
                    }
                    for (u32 index = 0; options && index < options->definition_count && valid; index += 1)
                    {
                        valid = compiler_embed_append_option(module->arena, arguments, &count, S8("-D"), options->definitions[index]);
                    }
                    for (u32 index = 0; options && index < options->argument_count && valid; index += 1)
                    {
                        BusterEmbedString argument = options->arguments[index];
                        valid = compiler_embed_append_argument(
                            module->arena, arguments, &count, compiler_embed_internal_string(argument));
                    }
                    valid = valid && compiler_embed_append_argument(module->arena, arguments, &count, S8("--"));
                    valid = valid && compiler_embed_append_argument(
                        module->arena, arguments, &count, compiler_embed_internal_string(source_name));
                }
                if (!valid || !arguments)
                {
                    compiler_embed_set_driver_failure(module, COMPILER_DRIVER_ERROR_ARGUMENT, S8("invalid source or compile options"));
                    result = module->status;
                }
                else
                {
                    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(
                        module->arena, (SliceString8){.pointer = arguments, .length = count});
                    bool invocation_valid = invocation.error == COMPILER_DRIVER_ERROR_NONE &&
                                            invocation.action == COMPILER_DRIVER_ACTION_OBJECT &&
                                            invocation.input_count == 1 && !invocation.output_path.length;
                    if (!invocation_valid)
                    {
                        CompilerDriverError error = invocation.error == COMPILER_DRIVER_ERROR_NONE ? COMPILER_DRIVER_ERROR_ARGUMENT : invocation.error;
                        String8 message = invocation.diagnostic.length ? invocation.diagnostic : S8("embedding compile options must describe one -c object input");
                        compiler_embed_set_driver_failure(module, error, message);
                        result = module->status;
                    }
                    else
                    {
                        String8 source_copy = string_duplicate_arena(module->arena, compiler_embed_internal_string(source), true);
                        invocation.input_languages[0] = COMPILER_DRIVER_LANGUAGE_C;
                        invocation.input_language_count = 1;
                        invocation.language = COMPILER_DRIVER_LANGUAGE_C;
                        invocation.input_source = source_copy;
                        invocation.retain_object = true;
                        invocation.compile_jobs = 1;
                        module->result = compiler_driver_execute_invocation(module->arena, invocation);
                        module->status = compiler_embed_driver_status(module->result.error);
                        if (module->status != BUSTER_EMBED_STATUS_OK)
                        {
                            String8 message = module->result.diagnostic.length
                                                  ? module->result.diagnostic
                                                  : compiler_embed_driver_error_name(module->result.error);
                            compiler_embed_set_message(module, message);
                        }
                        compiler_embed_copy_diagnostics(module);
                        result = module->status;
                    }
                }
            }
            else
            {
                result = BUSTER_EMBED_STATUS_RESOURCE;
            }
        }
    }
    return result;
}

BusterEmbedStatus buster_embed_module_status(BusterEmbedModule const* module)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
    if (module)
    {
        result = module->status;
    }
    return result;
}

uint32_t buster_embed_module_diagnostic_count(BusterEmbedModule const* module)
{
    u32 result = 0;
    if (module)
    {
        result = module->diagnostic_count;
    }
    return result;
}

BusterEmbedDiagnostic buster_embed_module_diagnostic(BusterEmbedModule const* module, uint32_t index)
{
    BusterEmbedDiagnostic result = {0};
    if (module && index < module->diagnostic_count)
    {
        result = module->diagnostics[index];
    }
    return result;
}

BusterEmbedString buster_embed_module_message(BusterEmbedModule const* module)
{
    BusterEmbedString result = compiler_embed_empty_string();
    if (module)
    {
        result = module->message;
    }
    return result;
}

BusterEmbedStatus buster_embed_module_object(BusterEmbedModule* module, BusterEmbedBytes* out_bytes)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
    if (out_bytes)
    {
        *out_bytes = (BusterEmbedBytes){0};
        if (module)
        {
            if (module->status != BUSTER_EMBED_STATUS_OK)
            {
                result = BUSTER_EMBED_STATUS_NOT_COMPILED;
            }
            else
            {
                if (!module->object_ready)
                {
                    module->object_artifact = object_write(module->arena, &module->result.object, object_format_for_target(module->result.object.target));
                    module->object_ready = module->object_artifact.error == OBJECT_ERROR_NONE;
                    if (!module->object_ready)
                    {
                        module->message = compiler_embed_copy_string(
                            module, string_format(module->arena, S8("object serialization failed: error {u32}"), (u32)module->object_artifact.error));
                    }
                }
                if (module->object_ready)
                {
                    out_bytes->pointer = module->object_artifact.bytes.pointer;
                    out_bytes->length = (size_t)module->object_artifact.bytes.length;
                    result = BUSTER_EMBED_STATUS_OK;
                }
                else
                {
                    result = BUSTER_EMBED_STATUS_RESOURCE;
                }
            }
        }
    }
    return result;
}

BusterEmbedStatus buster_embed_module_load(BusterEmbedModule* module, BusterEmbedImport const* imports, uint32_t import_count)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
    if (module && (!import_count || imports))
    {
        if (module->status != BUSTER_EMBED_STATUS_OK)
        {
            result = BUSTER_EMBED_STATUS_NOT_COMPILED;
        }
        else if (module->loaded)
        {
            result = BUSTER_EMBED_STATUS_ALREADY_LOADED;
        }
        else
        {
            bool valid = true;
            for (u32 index = 0; index < import_count && valid; index += 1)
            {
                valid = compiler_embed_string_valid(imports[index].name) && imports[index].name.length && imports[index].address &&
                        (imports[index].kind == BUSTER_EMBED_SYMBOL_FUNCTION || imports[index].kind == BUSTER_EMBED_SYMBOL_DATA);
            }
            if (valid)
            {
                JitHostBinding* bindings = arena_allocate(module->arena, JitHostBinding, import_count);
                for (u32 index = 0; index < import_count; index += 1)
                {
                    bindings[index].name = compiler_embed_internal_string(imports[index].name);
                    bindings[index].name = string_duplicate_arena(module->arena, bindings[index].name, true);
                    bindings[index].address = imports[index].address;
                    bindings[index].kind = imports[index].kind == BUSTER_EMBED_SYMBOL_FUNCTION ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA;
                }
                JitProgram program = jit_link_object(
                    &module->result.object, (JitOptions){.bindings = bindings, .binding_count = import_count});
                if (program.error == JIT_ERROR_NONE)
                {
                    module->jit = program;
                    module->loaded = true;
                    module->message = compiler_embed_empty_string();
                    result = BUSTER_EMBED_STATUS_OK;
                }
                else
                {
                    result = compiler_embed_jit_status(program.error);
                    String8 detail = program.failing_symbol.length
                                         ? string_format(module->arena, S8("{S8}: {S8}"), jit_error_string(program.error), program.failing_symbol)
                                         : jit_error_string(program.error);
                    compiler_embed_set_message(module, detail);
                    jit_program_release(&program);
                }
            }
            else
            {
                result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
            }
        }
    }
    return result;
}

BusterEmbedStatus buster_embed_module_symbol(BusterEmbedModule* module, BusterEmbedString name, void** out_address)
{
    BusterEmbedStatus result = BUSTER_EMBED_STATUS_INVALID_ARGUMENT;
    if (out_address)
    {
        *out_address = 0;
        if (module && compiler_embed_string_valid(name) && name.length)
        {
            if (!module->loaded)
            {
                result = BUSTER_EMBED_STATUS_NOT_LOADED;
            }
            else
            {
                String8 internal_name = compiler_embed_internal_string(name);
                void* address = jit_program_symbol(&module->jit, internal_name);
                if (address)
                {
                    *out_address = address;
                    module->message = compiler_embed_empty_string();
                    result = BUSTER_EMBED_STATUS_OK;
                }
                else
                {
                    compiler_embed_set_message(
                        module, string_format(module->arena, S8("{S8}: {S8}"), jit_error_string(module->jit.error), internal_name));
                    result = BUSTER_EMBED_STATUS_SYMBOL_NOT_FOUND;
                }
            }
        }
    }
    return result;
}

void buster_embed_module_release(BusterEmbedModule* module)
{
    if (module)
    {
        BusterEmbedCompiler* compiler = module->compiler;
        if (module->loaded)
        {
            jit_program_release(&module->jit);
        }
        compiler->live_module_count -= 1;
        arena_destroy(module->arena, 1);
    }
}
