#pragma once

// Direct WebAssembly emitter.  It supports both core wasm32 modules (including
// WASI Preview 1 command modules) and Memory64 wasm64 modules.  The emitter
// intentionally consumes canonical typed IR, rather than target-machine IR:
// WebAssembly has a small scalar instruction vocabulary and keeping this
// boundary typed makes unsupported ABI shapes fail before any bytes are
// produced.

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/ir.h>

typedef enum Wasm64ErrorCode
{
    WASM64_ERROR_NONE,
    WASM64_ERROR_INVALID_ARGUMENT,
    WASM64_ERROR_IR_VALIDATION,
    WASM64_ERROR_UNSUPPORTED_TYPE,
    WASM64_ERROR_UNSUPPORTED_AGGREGATE_ABI,
    WASM64_ERROR_VARIADIC,
    WASM64_ERROR_INDIRECT_CALL,
    WASM64_ERROR_ATOMIC,
    WASM64_ERROR_SIMD,
    WASM64_ERROR_INLINE_ASSEMBLY,
    WASM64_ERROR_TLS,
    WASM64_ERROR_UNSUPPORTED_INSTRUCTION,
    WASM64_ERROR_UNRESOLVED_SYMBOL,
    WASM64_ERROR_DUPLICATE_SYMBOL,
    WASM64_ERROR_ENCODING,
    WASM64_ERROR_COUNT,
} Wasm64ErrorCode;

typedef struct Wasm64Error Wasm64Error;
struct Wasm64Error
{
    Wasm64ErrorCode code;
    String8 message;
    String8 diagnostic;
    IrFunctionId function;
    IrBlockId block;
    IrInstructionId instruction;
    IrSymbolId symbol;
    IrOpcode opcode;
};

typedef struct Wasm64Stats Wasm64Stats;
struct Wasm64Stats
{
    bool memory64;
    bool wasi_preview1;
    bool deterministic;
    u8 reserved[5];
    u32 module_count;
    u32 function_count;
    u32 defined_function_count;
    u32 import_count;
    u32 export_count;
    u32 type_count;
    u32 global_count;
    u32 data_segment_count;
    u32 block_count;
    u64 memory_min_pages;
    u64 memory_max_pages;
    u64 static_data_bytes;
    u64 stack_lower_bound;
    u64 stack_upper_bound;
    u64 code_bytes;
    u64 binary_bytes;
};

typedef struct Wasm64Artifact Wasm64Artifact;
struct Wasm64Artifact
{
    // `bytes`, `wasm`, and `binary` are aliases kept together so callers can
    // use whichever spelling is natural.  They always point at the same
    // arena-owned module bytes.
    ByteSlice bytes;
    ByteSlice wasm;
    ByteSlice binary;
    Wasm64Error error;
    Wasm64Stats stats;
    bool success;
    u8 reserved[7];
};

typedef struct Wasm64Options Wasm64Options;

typedef enum WasmEnvironment
{
    WASM_ENVIRONMENT_FREESTANDING,
    WASM_ENVIRONMENT_WASI_PREVIEW1,
    WASM_ENVIRONMENT_COUNT,
} WasmEnvironment;

struct Wasm64Options
{
    // Export memory as "memory" and reserve a complete 64 KiB stack above
    // static data. The minimum covers the stack; a zero maximum is unbounded.
    String8 memory_export_name;
    u64 initial_pages;
    u64 maximum_pages;
    WasmEnvironment environment;
    // 4 selects wasm32; 8 selects Memory64 wasm64.
    u8 pointer_size;
    bool export_memory;
    bool export_functions;
    bool deterministic;
    // For WASI Preview 1, synthesize the command `_start` adapter around a C
    // `main` when the program does not define `_start` itself.
    bool synthesize_start;
    // The caller already ran ir_prepare_canonical_module on these modules and
    // has not mutated them since, as the driver does; like
    // CodegenModuleOptions.assume_validated it skips the emitter's own
    // uncertified re-preparation. Zero keeps the validating default.
    bool assume_validated;
    u8 reserved[2];
};

#define WASM64_OPTIONS_DEFAULT                                                                                                             \
    ((Wasm64Options){.environment = WASM_ENVIRONMENT_FREESTANDING, .pointer_size = 8, .export_memory = true, .export_functions = true,       \
                     .deterministic = true})
#define WASM32_WASI_OPTIONS_DEFAULT                                                                                                        \
    ((Wasm64Options){.environment = WASM_ENVIRONMENT_WASI_PREVIEW1, .pointer_size = 4, .export_memory = true, .export_functions = true,      \
                     .deterministic = true, .synthesize_start = true})

// Generic spellings for new callers.  The Wasm64-prefixed names remain source
// compatible with the original Memory64-only API.
typedef Wasm64ErrorCode WasmErrorCode;
typedef Wasm64Error WasmError;
typedef Wasm64Stats WasmStats;
typedef Wasm64Artifact WasmArtifact;
typedef Wasm64Options WasmOptions;

#define WASM_OPTIONS_DEFAULT WASM64_OPTIONS_DEFAULT
#define WASM_OPTIONS_WASI_PREVIEW1 WASM32_WASI_OPTIONS_DEFAULT

BUSTER_F_DECL WasmArtifact wasm_emit(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count, WasmOptions options);
BUSTER_F_DECL WasmArtifact wasm_emit_program(Arena* arena, IrProgram* program, WasmOptions options);
BUSTER_F_DECL bool wasm_artifact_is_valid(WasmArtifact artifact);
BUSTER_F_DECL String8 wasm_error_code_name(WasmErrorCode code);

BUSTER_F_DECL Wasm64Artifact wasm64_emit(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count);
BUSTER_F_DECL Wasm64Artifact wasm64_emit_with_options(Arena* arena, IrProgram* program, IrModule* modules, u32 module_count,
                                                      Wasm64Options options);
BUSTER_F_DECL Wasm64Artifact wasm64_emit_program(Arena* arena, IrProgram* program);
BUSTER_F_DECL bool wasm64_artifact_is_valid(Wasm64Artifact artifact);
BUSTER_F_DECL String8 wasm64_error_code_name(Wasm64ErrorCode code);
