#pragma once
#include <buster/base.h>
#include <buster/system_headers.h>
#include <buster/compiler/ir/ir.h>
#include <buster/compiler/backend/code_generation.h>
#include <buster/compiler/backend/instruction_selection.h>
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
#if defined(_WIN32)
#include <buster/string16.c>
#endif
#include <buster/integer.c>
#include <buster/file.c>
#include <buster/compiler/ir/ir.c>
#include <buster/compiler/backend/instruction_selection.c>
#include <buster/compiler/backend/code_generation.c>
#include <buster/path.c>
#include <buster/test.c>
#include <buster/arguments.c>
#endif

ENUM(CompilerCommand, 
    COMPILER_COMMAND_TEST,
);

STRUCT(CCProgramState)
{
    ProgramState general_program_state;
    StringOs cwd;
    CompilerCommand command;
    bool test;
    u8 reserved[3];
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
    let absolute_path = string_os_join_arena(arena, (StringOsSlice)BUSTER_ARRAY_TO_SLICE(parts), true);
    let file_content = file_read(arena, options.relative_path, (FileReadOptions) {});

    return (File) {
        .absolute_path = absolute_path,
        .original_content = BYTE_SLICE_TO_STRING(8, file_content),
    };
}

BUSTER_GLOBAL_LOCAL bool preprocess_file(File* file)
{
    BUSTER_UNUSED(file);
    if (file) BUSTER_TRAP();
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

STRUCT(Module)
{

};

STRUCT(Token)
{
};

BUSTER_GLOBAL_LOCAL constexpr u64 buffer_token_count = 64;

STRUCT(Tokenizer)
{
    Token buffer[buffer_token_count];
};

STRUCT(TopLevelDeclaration)
{
};

STRUCT(Type)
{
};

BUSTER_GLOBAL_LOCAL TopLevelDeclaration* parse_top_level_declaration()
{
    // parse_type();
    return 0;
}

// BUSTER_GLOBAL_LOCAL ParsingResult parse_chunk(File file)
// {
//     String8 preprocessed = file.preprocessed_content;
//     parse_symbol_name();
//     BUSTER_UNUSED(file);
//     return (ParsingResult){};
// }

BUSTER_GLOBAL_LOCAL bool compile_file(Arena* arena, File* file, CompilerOptions options)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(file);
    BUSTER_UNUSED(options);
    if (!preprocess_file(file))
    {
        return false;
    }
    // let parsing = parse_chunk(*file);
    // if (!parsing_succeeded(parsing))
    // {
    //     return false;
    // }
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
    BUSTER_UNUSED(arguments);
    return compile(arguments->arena, SOs("preamble.c"), (CompilerOptions){});
    // let selector_unit_test = instruction_selection_tests(arguments);
    // let module = ir_create_mock_module(arguments->arena);
    // let functions = ir_module_get_functions(module);
    // let generation = module_generation_initialize();
    // unit_test_succeeded(selector_unit_test);
    // for (u64 i = 0; i < functions.length; i += 1)
    // {
    //     let function = &functions.pointer[i];
    //     result = result & function_generate(&generation, arguments->arena, module, function, (CodeGenerationOptions){});
    // }
    // return result;
}
#endif

#if BUSTER_FUZZING
BUSTER_IMPL s32 buster_fuzz(const u8* pointer, size_t size)
{
    BUSTER_UNUSED(pointer);
    BUSTER_UNUSED(size);
    return 0;
}
#else
BUSTER_IMPL ProcessResult process_arguments()
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    let argv = program_state->input.argv;
    let envp = program_state->input.envp;

    let arg_it = string_os_list_iterator_initialize(argv);

    string_os_list_iterator_next(&arg_it);

    u64 i = 1;

    for (let arg = string_os_list_iterator_next(&arg_it); arg.pointer; arg = string_os_list_iterator_next(&arg_it), i += 1)
    {
        if (!string_os_equal(arg, SOs("test")))
        {
            let r = buster_argument_process(argv, envp, i, arg);
            if (r != PROCESS_RESULT_SUCCESS)
            {
                string8_print(S8("Failed to process argument {SOs}\n"), arg);
                result = r;
                break;
            }
        }
        else
        {
            cc_program_state.test = true;
        }
    }
    return result;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    UnitTestArguments arguments = { thread_arena(), &default_show };
    bool result;
    if (cc_program_state.test)
    {
        result = compiler_tests(&arguments);
    }
    else
    {
        result = compile(arguments.arena, SOs("preamble.c"), (CompilerOptions){});
    }
    return result ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
#endif
