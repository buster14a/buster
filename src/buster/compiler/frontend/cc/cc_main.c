#pragma once
#include <buster/lib.h>
#include <buster/system_headers.h>

STRUCT(CCProgramState)
{
    ProgramState general_program_state;
    String8 cwd;
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
    String8 cwd;
};

STRUCT(File)
{
    String8 absolute_path;
    String8 original_content;
    String8 preprocessed_content;
};

STRUCT(CompilerFileReadOptions)
{
    String8 cwd;
    String8 relative_path;
};

BUSTER_LOCAL File file_from_path(Arena* arena, CompilerFileReadOptions options)
{
    String8 parts[] = {
        options.cwd,
        S8("/"),
        options.relative_path,
    };
    let absolute_path = arena_join_string8(arena, BUSTER_ARRAY_TO_SLICE(String8Slice, parts), true);
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

BUSTER_LOCAL bool compile(Arena* arena, String8 relative_path, CompilerOptions options)
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
    printf("Hello world from the compiler\n");
    let basic = OsS("test/cc/basic.c");
    let result = compile(arena, basic, (CompilerOptions) {});
    if (!result)
    {
        printf("Compilation failed!\n");
    }
    return result ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
