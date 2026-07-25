#pragma once

#include <buster/compiler/object/object.h>

typedef enum LinkError
{
    LINK_ERROR_NONE,
    LINK_ERROR_INVALID_INPUT,
    LINK_ERROR_TARGET_MISMATCH,
    LINK_ERROR_DUPLICATE_SYMBOL,
    LINK_ERROR_UNRESOLVED_SYMBOL,
    LINK_ERROR_OBJECT_WRITE,
    LINK_ERROR_FILE_WRITE,
    LINK_ERROR_PROCESS_SPAWN,
    LINK_ERROR_PROCESS_FAILED,
    LINK_ERROR_UNSUPPORTED_HOST,
    LINK_ERROR_COUNT,
} LinkError;

typedef struct LinkOptions LinkOptions;
struct LinkOptions
{
    bool allow_undefined_symbols;
    u8 reserved[7];
};

typedef struct LinkObjectResult LinkObjectResult;
struct LinkObjectResult
{
    ObjectFile object;
    String8 symbol;
    LinkError error;
};

typedef struct LibcLinkOptions LibcLinkOptions;
struct LibcLinkOptions
{
    String8 output_path;
    String8 object_path;
    String8 linker_executable;
};

typedef struct LibcLinkResult LibcLinkResult;
struct LibcLinkResult
{
    ByteSlice standard_output;
    ByteSlice standard_error;
    ProcessResult process_result;
    LinkError error;
};

BUSTER_F_DECL LinkObjectResult link_objects(
    Arena* arena,
    ObjectFile* objects,
    u32 object_count,
    LinkOptions options);
BUSTER_F_DECL LibcLinkResult link_object_with_libc(
    Arena* arena,
    ObjectFile* object,
    LibcLinkOptions options);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult link_tests(
    UnitTestArguments* arguments);
#endif
