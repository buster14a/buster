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
typedef struct CodegenAbiPart CodegenAbiPart;
struct CodegenAbiPart
{
    u32 index;
    u32 stack_offset;
    u32 value_offset;
    u32 size;
    CodegenAbiLocationKind kind;
};

enum
{
    CODEGEN_ABI_MAX_PARTS = ANALYSIS_ABI_MAX_PARTS,
};

struct CodegenAbiLocation
{
    CodegenAbiPart parts[CODEGEN_ABI_MAX_PARTS];
    u32 index;
    u32 stack_offset;
    u32 indirect_copy_offset;
    CodegenAbiLocationKind kind;
    u32 part_count;
    bool indirect;
    u8 reserved[3];
};

typedef struct CodegenAbiSignature CodegenAbiSignature;
struct CodegenAbiSignature
{
    CodegenAbiLocation* arguments;
    CodegenAbiLocation result;
    u32 argument_count;
    u32 stack_size;
    u32 indirect_result_register;
    bool valid;
    u8 reserved[3];
};

typedef struct CodegenFunction CodegenFunction;
typedef struct CodegenCallRelocation CodegenCallRelocation;
struct CodegenCallRelocation
{
    CodegenCallRelocation* next;
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    u32 displacement_offset;
    bool aarch64;
    u8 reserved[3];
};

struct CodegenFunction
{
    ByteSlice code;
    CodegenCallRelocation* first_call_relocation;
    CodegenError error;
    CodegenAbi abi;
    u32 stack_frame_size;
    u32 register_value_count;
    u32 spilled_value_count;
};

typedef struct CodegenModuleEntry CodegenModuleEntry;
struct CodegenModuleEntry
{
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    u32 offset;
};

typedef struct CodegenModule CodegenModule;
struct CodegenModule
{
    ByteSlice code;
    CodegenModuleEntry* entries;
    CodegenError error;
    CodegenAbi abi;
    u32 entry_count;
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
BUSTER_F_DECL CodegenModule codegen_generate_module(
    Arena* arena,
    AnalysisResult* analysis,
    IrModule* module,
    Target target);
BUSTER_F_DECL CodegenExecutable codegen_make_executable(
    CodegenFunction function);
BUSTER_F_DECL void codegen_release_executable(
    CodegenExecutable executable);

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult codegen_tests(UnitTestArguments* arguments);
#endif
