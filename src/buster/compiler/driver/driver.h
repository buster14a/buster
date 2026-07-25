#pragma once

#include <buster/compiler/link/link.h>

typedef enum CompilerDriverError
{
    COMPILER_DRIVER_ERROR_NONE,
    COMPILER_DRIVER_ERROR_INVALID_INPUT,
    COMPILER_DRIVER_ERROR_FILE_READ,
    COMPILER_DRIVER_ERROR_TOKENIZE,
    COMPILER_DRIVER_ERROR_PARSE,
    COMPILER_DRIVER_ERROR_ANALYSIS,
    COMPILER_DRIVER_ERROR_IR,
    COMPILER_DRIVER_ERROR_CODEGEN,
    COMPILER_DRIVER_ERROR_OBJECT,
    COMPILER_DRIVER_ERROR_LINK,
    COMPILER_DRIVER_ERROR_COUNT,
} CompilerDriverError;

typedef struct CompilerDriverOptions CompilerDriverOptions;
struct CompilerDriverOptions
{
    String8 source_path;
    String8 output_path;
    String8 object_path;
    String8 linker_executable;
    String8 module_root;
    Target target;
};

typedef struct CompilerDriverResult CompilerDriverResult;
struct CompilerDriverResult
{
    String8 diagnostic;
    LibcLinkResult link;
    CompilerDriverError error;
    CodegenError codegen_error;
    ObjectError object_error;
    u32 tokenizer_error_count;
    u32 parser_diagnostic_count;
    u32 analysis_diagnostic_count;
};

BUSTER_F_DECL CompilerDriverResult
compiler_driver_compile_with_libc(
    Arena* arena,
    CompilerDriverOptions options);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult compiler_driver_tests(
    UnitTestArguments* arguments);
#endif
