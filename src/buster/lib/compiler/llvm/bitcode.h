#pragma once

// Native LLVM bitcode emitter. The writer consumes canonical typed IR and
// serializes the LLVM bitstream directly; it does not link against LLVM and
// does not route through textual LLVM IR.

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/ir.h>

typedef enum LlvmBitcodeErrorCode
{
    LLVM_BITCODE_ERROR_NONE,
    LLVM_BITCODE_ERROR_INVALID_ARGUMENT,
    LLVM_BITCODE_ERROR_IR_VALIDATION,
    LLVM_BITCODE_ERROR_UNSUPPORTED_TYPE,
    LLVM_BITCODE_ERROR_UNSUPPORTED_INSTRUCTION,
    LLVM_BITCODE_ERROR_UNSUPPORTED_GLOBAL_INITIALIZER,
    LLVM_BITCODE_ERROR_UNRESOLVED_SYMBOL,
    LLVM_BITCODE_ERROR_DUPLICATE_SYMBOL,
    LLVM_BITCODE_ERROR_VALUE_NUMBERING,
    LLVM_BITCODE_ERROR_ENCODING,
    LLVM_BITCODE_ERROR_COUNT,
} LlvmBitcodeErrorCode;

typedef struct LlvmBitcodeError LlvmBitcodeError;
struct LlvmBitcodeError
{
    LlvmBitcodeErrorCode code;
    String8 message;
    String8 diagnostic;
    IrFunctionId function;
    IrBlockId block;
    IrInstructionId instruction;
    IrSymbolId symbol;
    IrOpcode opcode;
};

typedef struct LlvmBitcodeStats LlvmBitcodeStats;
struct LlvmBitcodeStats
{
    bool deterministic;
    u8 reserved[3];
    u32 module_count;
    u32 function_count;
    u32 defined_function_count;
    u32 global_count;
    u32 type_count;
    u32 constant_count;
    u32 block_count;
    u32 instruction_count;
    u64 binary_bytes;
};

typedef struct LlvmBitcodeArtifact LlvmBitcodeArtifact;
struct LlvmBitcodeArtifact
{
    // All three spellings alias the same arena-owned byte range.
    ByteSlice bytes;
    ByteSlice bitcode;
    ByteSlice binary;
    LlvmBitcodeError error;
    LlvmBitcodeStats stats;
    bool success;
    u8 reserved[7];
};

typedef struct LlvmBitcodeOptions LlvmBitcodeOptions;
struct LlvmBitcodeOptions
{
    // Empty strings omit the corresponding optional module records. LLVM
    // consumers may then select their own target while loading the module.
    String8 target_triple;
    String8 data_layout;
    String8 source_filename;
    bool deterministic;
    bool validate_ir;
    u8 reserved[6];
};

#define LLVM_BITCODE_OPTIONS_DEFAULT ((LlvmBitcodeOptions){.deterministic = true, .validate_ir = true})

BUSTER_F_DECL LlvmBitcodeArtifact llvm_bitcode_emit(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count);
BUSTER_F_DECL LlvmBitcodeArtifact llvm_bitcode_emit_with_options(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count,
                                                                 LlvmBitcodeOptions options);
BUSTER_F_DECL LlvmBitcodeArtifact llvm_bitcode_emit_program(Arena* arena, IrProgram* program);
BUSTER_F_DECL bool llvm_bitcode_artifact_is_valid(LlvmBitcodeArtifact artifact);
BUSTER_F_DECL String8 llvm_bitcode_error_code_name(LlvmBitcodeErrorCode code);
