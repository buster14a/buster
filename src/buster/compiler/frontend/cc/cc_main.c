#pragma once
#include <buster/lib.h>
#include <buster/system_headers.h>

STRUCT(CCProgramState)
{
    ProgramState general_program_state;
    OsString cwd;
};

BUSTER_LOCAL CCProgramState cc_program_state = {

};

BUSTER_IMPL ProgramState* program_state = &cc_program_state.general_program_state;

BUSTER_IMPL ProcessResult process_arguments()
{
    return PROCESS_RESULT_SUCCESS;
}

STRUCT(CompilerOptions)
{
    OsString cwd;
};

STRUCT(File)
{
    OsString absolute_path;
    String8 original_content;
    String8 preprocessed_content;
};

STRUCT(CompilerFileReadOptions)
{
    OsString cwd;
    OsString relative_path;
};

BUSTER_LOCAL File file_from_path(Arena* arena, CompilerFileReadOptions options)
{
    OsString parts[] = {
        options.cwd,
        OsS("/"),
        options.relative_path,
    };
    let absolute_path = arena_join_os_string(arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, parts), true);
    let file_content = file_read(arena, options.relative_path, (FileReadOptions) {});

    return (File) {
        .absolute_path = absolute_path,
        .original_content = file_content,
    };
}

BUSTER_LOCAL bool preprocess_file(File* file)
{
    // TODO
    file->preprocessed_content = file->original_content;
    return true;
}

STRUCT(ParsingResult)
{
    bool success;
};

BUSTER_LOCAL bool parsing_succeeded(ParsingResult result)
{
    BUSTER_UNUSED(result);
    return true;
}

BUSTER_LOCAL ParsingResult parse_chunk(File file)
{
    BUSTER_UNUSED(file);
    return (ParsingResult){};
}

BUSTER_LOCAL bool compile_file(Arena* arena, File* file, CompilerOptions options)
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

BUSTER_LOCAL bool compile(Arena* arena, OsString relative_path, CompilerOptions options)
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

BUSTER_IMPL ProcessResult thread_entry_point()
{
    let arena = thread->arena;
    cc_program_state.cwd = path_absolute(arena, OsS("."));
    BUSTER_UNUSED(thread);
    print(S8("Hello world from the compiler\n"));
    let basic = OsS("test/cc/basic.c");
    let result = compile(arena, basic, (CompilerOptions) {});
    if (!result)
    {
        print(S8("Compilation failed!\n"));
    }
    return result ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
