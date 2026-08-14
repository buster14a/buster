#pragma once

// Direct Linux eBPF emitter. The backend consumes canonical typed IR and
// produces little-endian ELF64 ET_REL objects for EM_BPF. Keeping this path
// separate from native Machine IR avoids imposing the verifier's register,
// stack, and relocation model on native instruction selection or scheduling.

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/ir.h>

typedef enum EbpfErrorCode
{
    EBPF_ERROR_NONE,
    EBPF_ERROR_INVALID_ARGUMENT,
    EBPF_ERROR_IR_VALIDATION,
    EBPF_ERROR_UNSUPPORTED_TYPE,
    EBPF_ERROR_UNSUPPORTED_AGGREGATE,
    EBPF_ERROR_UNSUPPORTED_INSTRUCTION,
    EBPF_ERROR_UNSUPPORTED_ABI,
    EBPF_ERROR_VARIADIC,
    EBPF_ERROR_INDIRECT_CALL,
    EBPF_ERROR_ATOMIC,
    EBPF_ERROR_SIMD,
    EBPF_ERROR_INLINE_ASSEMBLY,
    EBPF_ERROR_TLS,
    EBPF_ERROR_STACK_LIMIT,
    EBPF_ERROR_JUMP_RANGE,
    EBPF_ERROR_UNRESOLVED_SYMBOL,
    EBPF_ERROR_DUPLICATE_SYMBOL,
    EBPF_ERROR_BTF,
    EBPF_ERROR_ENCODING,
    EBPF_ERROR_COUNT,
} EbpfErrorCode;

typedef struct EbpfError EbpfError;
struct EbpfError
{
    EbpfErrorCode code;
    String8 message;
    String8 diagnostic;
    IrFunctionId function;
    IrBlockId block;
    IrInstructionId instruction;
    IrSymbolId symbol;
    IrOpcode opcode;
};

typedef struct EbpfStats EbpfStats;
struct EbpfStats
{
    bool little_endian;
    bool deterministic;
    bool has_btf;
    u8 reserved[5];
    u32 module_count;
    u32 function_count;
    u32 program_count;
    u32 subprogram_count;
    u32 global_count;
    u32 map_count;
    u32 section_count;
    u32 relocation_count;
    u32 symbol_count;
    u32 max_stack_bytes;
    u64 instruction_count;
    u64 code_bytes;
    u64 data_bytes;
    u64 btf_bytes;
    u64 object_bytes;
};

typedef struct EbpfOptions EbpfOptions;
struct EbpfOptions
{
    // BTF is enabled by default because current libbpf map declarations use
    // BTF-defined SEC(".maps") objects. This emits .BTF, but deliberately not
    // .BTF.ext or CO-RE relocation records yet.
    bool emit_btf;
    bool deterministic;
    u8 reserved[6];
};

#define EBPF_OPTIONS_DEFAULT ((EbpfOptions){.emit_btf = true, .deterministic = true})

typedef struct EbpfArtifact EbpfArtifact;
struct EbpfArtifact
{
    ByteSlice bytes;
    ByteSlice object;
    ByteSlice elf;
    EbpfError error;
    EbpfStats stats;
    bool success;
    u8 reserved[7];
};

BUSTER_F_DECL EbpfArtifact ebpf_emit(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count);
BUSTER_F_DECL EbpfArtifact ebpf_emit_with_options(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count,
                                                  EbpfOptions options);
BUSTER_F_DECL EbpfArtifact ebpf_emit_program(Arena* arena, IrProgram* program);
BUSTER_F_DECL bool ebpf_artifact_is_valid(EbpfArtifact artifact);
BUSTER_F_DECL String8 ebpf_error_code_name(EbpfErrorCode code);
