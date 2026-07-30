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
    CODEGEN_ABI_AARCH64_WINDOWS,
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
typedef enum CodegenDataRelocationKind
{
    CODEGEN_DATA_RELOCATION_X86_64_PC32,
    CODEGEN_DATA_RELOCATION_ABSOLUTE64,
    CODEGEN_DATA_RELOCATION_COUNT,
} CodegenDataRelocationKind;

typedef struct CodegenDataRelocation CodegenDataRelocation;
struct CodegenDataRelocation
{
    CodegenDataRelocation* next;
    u32 code_offset;
    u32 data_offset;
    CodegenDataRelocationKind kind;
};

struct CodegenCallRelocation
{
    CodegenCallRelocation* next;
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    u32 displacement_offset;
    bool aarch64;
    bool absolute;
    u8 reserved[2];
};

struct CodegenFunction
{
    ByteSlice code;
    ByteSlice read_only_data;
    CodegenCallRelocation* first_call_relocation;
    CodegenDataRelocation* first_data_relocation;
    CodegenError error;
    CodegenAbi abi;
    u32 stack_frame_size;
    u32 register_value_count;
    u32 spilled_value_count;
    u32 native_vector_operation_count;
    u32 split_vector_operation_count;
    u32 vzeroupper_count;
    u32 forwarded_wide_vector_load_count;
};

typedef struct CodegenModuleEntry CodegenModuleEntry;
struct CodegenModuleEntry
{
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    IrSymbolId symbol;
    u32 offset;
};

typedef struct CodegenModuleRelocation CodegenModuleRelocation;
typedef enum CodegenModuleRelocationSource
{
    CODEGEN_MODULE_RELOCATION_CODE,
    CODEGEN_MODULE_RELOCATION_READ_ONLY_DATA,
    CODEGEN_MODULE_RELOCATION_DATA,
    CODEGEN_MODULE_RELOCATION_THREAD_LOCAL_DATA,
    CODEGEN_MODULE_RELOCATION_SOURCE_COUNT,
} CodegenModuleRelocationSource;

struct CodegenModuleRelocation
{
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    IrSymbolId symbol;
    s64 addend;
    u32 offset;
    CodegenModuleRelocationSource source;
    bool aarch64;
    bool absolute;
    bool is_thread_local;
    bool thread_local_low;
    bool thread_local_index;
    u8 reserved;
};

typedef struct CodegenModuleDataRelocation
    CodegenModuleDataRelocation;
struct CodegenModuleDataRelocation
{
    u32 code_offset;
    u32 data_offset;
    CodegenDataRelocationKind kind;
};

typedef struct CodegenModuleGlobal CodegenModuleGlobal;
struct CodegenModuleGlobal
{
    IrSymbolId symbol;
    u32 offset;
    u32 size;
    u32 alignment;
    bool read_only;
    bool is_thread_local;
    bool zero_fill;
    u8 reserved;
};

typedef struct CodegenModule CodegenModule;
typedef struct CodegenStatistics CodegenStatistics;
struct CodegenStatistics
{
    u64 instruction_count;
    u64 value_count;
    u64 stack_value_bytes;
    u64 stack_frame_bytes;
    u64 code_bytes;
    u64 native_vector_operation_count;
    u64 split_vector_operation_count;
    u64 vzeroupper_count;
    u64 forwarded_wide_vector_load_count;
    u32 function_count;
    u32 maximum_stack_frame_bytes;
};

struct CodegenModule
{
    ByteSlice code;
    ByteSlice read_only_data;
    ByteSlice writable_data;
    ByteSlice thread_local_data;
    u64 thread_local_zero_size;
    CodegenModuleEntry* entries;
    CodegenModuleGlobal* globals;
    CodegenModuleRelocation* relocations;
    CodegenModuleDataRelocation* data_relocations;
    CodegenError error;
    CodegenAbi abi;
    CodegenStatistics statistics;
    IrFunctionId failed_function;
    IrInstructionId failed_instruction;
    IrOpcode failed_opcode;
    u32 entry_count;
    u32 global_count;
    u32 relocation_count;
    u32 data_relocation_count;
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
BUSTER_F_DECL CodegenModule
codegen_generate_canonical_module(
    Arena* arena,
    IrProgram* program,
    IrModule* module,
    Target target);
BUSTER_F_DECL CodegenExecutable codegen_make_executable(
    CodegenFunction function);
BUSTER_F_DECL void codegen_release_executable(
    CodegenExecutable executable);

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult codegen_tests(UnitTestArguments* arguments);
#endif
