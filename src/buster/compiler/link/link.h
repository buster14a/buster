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
    LINK_ERROR_ENTRY_SYMBOL,
    LINK_ERROR_RELOCATION,
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

typedef struct NativeExecutableLinkOptions NativeExecutableLinkOptions;
struct NativeExecutableLinkOptions
{
    String8 output_path;
    String8 entry_symbol;
};

typedef struct NativeExecutableLinkResult NativeExecutableLinkResult;
struct NativeExecutableLinkResult
{
    ByteSlice executable;
    String8 symbol;
    LinkError error;
};

BUSTER_F_DECL LinkObjectResult link_objects(
    Arena* arena,
    ObjectFile* objects,
    u32 object_count,
    LinkOptions options);
BUSTER_F_DECL NativeExecutableLinkResult
link_native_executable(
    Arena* arena,
    ObjectFile* object,
    NativeExecutableLinkOptions options);
#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult link_tests(
    UnitTestArguments* arguments);
#endif
