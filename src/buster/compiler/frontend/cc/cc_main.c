#pragma once
#include <buster/base.h>
#include <buster/system_headers.h>
#include <buster/compiler/ir/ir.h>
#include <buster/compiler/backend/code_generation.h>
#include <buster/file.h>
#include <buster/path.h>
#include <buster/string8.h>

#include <buster/entry_point.h>

#if BUSTER_UNITY_BUILD
#include <buster/entry_point.c>
#include <buster/target.c>
#if defined(__x86_64__)
#include <buster/x86_64.c>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.c>
#endif
#include <buster/assertion.c>
#include <buster/memory.c>
#include <buster/string8.c>
#include <buster/os.c>
#include <buster/string.c>
#include <buster/arena.c>
#include <buster/string_os.c>
#if _WIN32
#include <buster/string16.c>
#endif
#include <buster/integer.c>
#include <buster/file.c>
#include <buster/compiler/ir/ir.c>
#include <buster/compiler/backend/code_generation.c>
#include <buster/path.c>
#include <buster/test.c>
#endif

ENUM(CompilerCommand, 
    COMPILER_COMMAND_TEST,
);

STRUCT(CCProgramState)
{
    ProgramState general_program_state;
    StringOs cwd;
    CompilerCommand command;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL CCProgramState cc_program_state = {};

BUSTER_IMPL ProgramState* program_state = &cc_program_state.general_program_state;

STRUCT(CompilerOptions)
{
    StringOs cwd;
};

STRUCT(File)
{
    StringOs absolute_path;
    String8 original_content;
    String8 preprocessed_content;
};

STRUCT(CompilerFileReadOptions)
{
    StringOs cwd;
    StringOs relative_path;
};

BUSTER_GLOBAL_LOCAL File file_from_path(Arena* arena, CompilerFileReadOptions options)
{
    StringOs parts[] = {
        options.cwd,
        SOs("/"),
        options.relative_path,
    };
    let absolute_path = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, parts), true);
    let file_content = file_read(arena, options.relative_path, (FileReadOptions) {});

    return (File) {
        .absolute_path = absolute_path,
        .original_content = BYTE_SLICE_TO_STRING(8, file_content),
    };
}

BUSTER_GLOBAL_LOCAL bool preprocess_file(File* file)
{
    // TODO
    file->preprocessed_content = file->original_content;
    return true;
}

STRUCT(ParsingResult)
{
    bool success;
};

BUSTER_GLOBAL_LOCAL bool parsing_succeeded(ParsingResult result)
{
    BUSTER_UNUSED(result);
    return true;
}

BUSTER_GLOBAL_LOCAL ParsingResult parse_chunk(File file)
{
    BUSTER_UNUSED(file);
    return (ParsingResult){};
}

BUSTER_GLOBAL_LOCAL bool compile_file(Arena* arena, File* file, CompilerOptions options)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(file);
    BUSTER_UNUSED(options);
    if (!preprocess_file(file))
    {
        return false;
    }
    let parsing = parse_chunk(*file);
    if (!parsing_succeeded(parsing))
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool compile(Arena* arena, StringOs relative_path, CompilerOptions options)
{
    if (!options.cwd.pointer)
    {
        options.cwd = cc_program_state.cwd;
    }

    let file = file_from_path(arena, (CompilerFileReadOptions) {
        .cwd = options.cwd,
        .relative_path = relative_path,
    });
    return compile_file(arena, &file, options);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool compiler_tests(UnitTestArguments* arguments)
{
    let module = ir_create_mock_module(arguments->arena);
    let functions = ir_module_get_functions(module);
    let generation = module_generation_initialize();
    for (u64 i = 0; i < functions.length; i += 1)
    {
        let function = &functions.pointer[i];
        function_generate(&generation, arguments->arena, module, function, (CodeGenerationOptions){});
    }
    return true;
}
#endif

#if BUSTER_FUZZING
BUSTER_DECL s32 buster_fuzz(const u8* pointer, size_t size)
{
    BUSTER_UNUSED(pointer);
    BUSTER_UNUSED(size);
    return 0;
}
#else
BUSTER_IMPL ProcessResult process_arguments()
{
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    UnitTestArguments arguments = { thread_arena(), &default_show };
    let arena = arguments.arena;
    string8_print(S8("Hello world from the compiler\n"));
    let result = library_tests(&arguments);
    cc_program_state.cwd = path_absolute_arena(arena, SOs("."));
    let basic = SOs("tests/cc/basic.c");
    let compile_result = compile(arena, basic, (CompilerOptions) {});
    if (!compile_result)
    {
        string8_print(S8("Compilation failed!\n"));
    }

    switch (cc_program_state.command)
    {
        break; case COMPILER_COMMAND_TEST:
        {
#if BUSTER_INCLUDE_TESTS
            UnitTestArguments test_arguments = {
                .arena = arena,
            };
            compile_result = compile_result & ir_tests(&test_arguments);
            compile_result = compile_result & compiler_tests(&test_arguments);
#else
            compile_result = false;
#endif
        }
        break; default: compile_result = false;
    }

    return (compile_result && batch_test_report(&arguments, result)) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
#endif
