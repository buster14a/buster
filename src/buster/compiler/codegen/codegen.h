#pragma once

#include <buster/compiler/ir/ir.h>
#include <buster/target.h>

typedef enum CodegenError
{
    CODEGEN_ERROR_NONE,
    CODEGEN_ERROR_UNSUPPORTED_TARGET,
    CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION,
    CODEGEN_ERROR_UNSUPPORTED_ABI,
    CODEGEN_ERROR_INVALID_IR,
    CODEGEN_ERROR_CAPACITY,
    CODEGEN_ERROR_EXECUTABLE_MEMORY,
    CODEGEN_ERROR_COUNT,
} CodegenError;

typedef enum CodegenAbi
{
    CODEGEN_ABI_X86_64_SYSTEM_V,
    CODEGEN_ABI_X86_64_WINDOWS,
    CODEGEN_ABI_AARCH64_AAPCS64,
    CODEGEN_ABI_AARCH64_DARWIN,
    CODEGEN_ABI_COUNT,
} CodegenAbi;

typedef enum CodegenAbiLocationKind
{
    CODEGEN_ABI_LOCATION_INTEGER_REGISTER,
    CODEGEN_ABI_LOCATION_FLOAT_REGISTER,
    CODEGEN_ABI_LOCATION_STACK,
    CODEGEN_ABI_LOCATION_INDIRECT,
    CODEGEN_ABI_LOCATION_COUNT,
} CodegenAbiLocationKind;

typedef struct CodegenAbiLocation CodegenAbiLocation;
struct CodegenAbiLocation
{
    u32 index;
    u32 stack_offset;
    CodegenAbiLocationKind kind;
    u32 reserved;
};

typedef struct CodegenAbiSignature CodegenAbiSignature;
struct CodegenAbiSignature
{
    CodegenAbiLocation* arguments;
    CodegenAbiLocation result;
    u32 argument_count;
    u32 stack_size;
};

typedef struct CodegenFunction CodegenFunction;
struct CodegenFunction
{
    ByteSlice code;
    CodegenError error;
    CodegenAbi abi;
    u32 stack_frame_size;
};

typedef struct CodegenExecutable CodegenExecutable;
struct CodegenExecutable
{
    void* address;
    u64 allocation_size;
    CodegenError error;
};

BUSTER_F_DECL CodegenAbi codegen_abi_for_target(Target target);
BUSTER_F_DECL CodegenAbiSignature codegen_classify_signature(
    Arena* arena,
    AnalysisResult* analysis,
    AnalysisTypeId function_type,
    CodegenAbi abi);
BUSTER_F_DECL CodegenFunction codegen_generate_function(
    Arena* arena,
    AnalysisResult* analysis,
    IrFunction* function,
    Target target);
BUSTER_F_DECL CodegenExecutable codegen_make_executable(
    CodegenFunction function);
BUSTER_F_DECL void codegen_release_executable(
    CodegenExecutable executable);

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult codegen_tests(UnitTestArguments* arguments);
#endif
