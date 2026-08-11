#pragma once

#include <buster/lib/compiler/debug/debug.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/target.h>

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
typedef enum CodegenUnwindActionKind
{
    CODEGEN_UNWIND_ACTION_PUSH_REGISTER,
    CODEGEN_UNWIND_ACTION_SAVE_REGISTER,
    CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER,
    CODEGEN_UNWIND_ACTION_ALLOCATE_STACK,
    CODEGEN_UNWIND_ACTION_NOP,
    CODEGEN_UNWIND_ACTION_COUNT,
} CodegenUnwindActionKind;

typedef struct CodegenUnwindAction CodegenUnwindAction;
struct CodegenUnwindAction
{
    // Offset of the first instruction after this prolog operation.
    u32 code_offset;
    // Stack bytes for ALLOCATE_STACK, or the post-operation SP-relative
    // offset for SAVE_REGISTER and SET_FRAME_POINTER.
    u32 value;
    CodegenUnwindActionKind kind;
    u8 register_index;
    u8 reserved[3];
};

typedef struct CodegenFunctionDescriptor CodegenFunctionDescriptor;
struct CodegenFunctionDescriptor
{
    CodegenUnwindAction* unwind_actions;
    u32* epilog_offsets;
    IrSymbolId symbol;
    u32 code_offset;
    u32 code_size;
    u32 prolog_size;
    u32 unwind_action_count;
    u32 epilog_count;
};

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

typedef struct CodegenLineEntry CodegenLineEntry;
struct CodegenLineEntry
{
    u32 code_offset;
    u32 source;
    u32 line;
    u32 column;
};

struct CodegenFunction
{
    ByteSlice code;
    ByteSlice read_only_data;
    CodegenFunctionDescriptor descriptor;
    CodegenCallRelocation* first_call_relocation;
    CodegenDataRelocation* first_data_relocation;
    CodegenLineEntry* line_entries;
    DebugLocationSeed* debug_locations;
    IrSymbolId symbol;
    u32 line_entry_count;
    u32 debug_location_count;
    CodegenError error;
    CodegenAbi abi;
    u32 stack_frame_size;
    u32 register_value_count;
    u32 spilled_value_count;
    u32 native_vector_operation_count;
    u32 split_vector_operation_count;
    u32 vzeroupper_count;
    u32 forwarded_wide_vector_load_count;
    u32 simd_operation_count;
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

// The relocation kind is deliberately stored in the byte that used to be
// reserved at the tail of CodegenModuleRelocation.  Keep this list in the
// same (format-neutral) vocabulary as ObjectRelocationKind: conversion to a
// native object must not infer a kind from a collection of loosely-related
// booleans.  The enum is represented by u8 in the record so adding a family
// does not change its ABI or alignment.
typedef enum CodegenModuleRelocationKind
{
    // Zero remains the historical x86-64 rel32 default.  This keeps old
    // zero-initialized records source-compatible while all producers now set
    // the field explicitly.
    CODEGEN_MODULE_RELOCATION_X86_64_PC32,
    CODEGEN_MODULE_RELOCATION_AARCH64_CALL26,
    CODEGEN_MODULE_RELOCATION_ABSOLUTE32,
    CODEGEN_MODULE_RELOCATION_ABSOLUTE64,
    CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32,
    CODEGEN_MODULE_RELOCATION_X86_64_PE_TLS_INDEX_PC32,
    CODEGEN_MODULE_RELOCATION_PE_TLS_OFFSET32,
    CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP,
    CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12,
    CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_OFFSET12,
    CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12,
    CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12,
    CODEGEN_MODULE_RELOCATION_X86_64_MACH_TLV_PC32,
    CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGE21,
    CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12,
    CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGE21,
    CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGEOFF12,
    CODEGEN_MODULE_RELOCATION_COUNT,
} CodegenModuleRelocationKind;

struct CodegenModuleRelocation
{
    AnalysisEntityId entity;
    AnalysisInstantiationId instantiation;
    IrSymbolId symbol;
    s64 addend;
    u32 offset;
    CodegenModuleRelocationSource source;
    IrBlockId label_block;
    bool aarch64;
    bool absolute;
    bool label_address;
    bool is_thread_local;
    bool thread_local_low;
    bool thread_local_index;
    // Legacy compatibility bits.  `kind` is authoritative; these are kept
    // only so old internal callers retain their ABI and so the independent
    // TLS-index/low and absolute-width semantics remain inspectable while
    // migration is in progress.  Producers must keep them consistent with
    // kind; codegen_module_relocation_valid() rejects mismatches.
    u8 kind;
};

BUSTER_CT_CHECK((u32)CODEGEN_MODULE_RELOCATION_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK(BUSTER_ALIGN_OF(CodegenModuleRelocation) == 8);
BUSTER_CT_CHECK(sizeof(CodegenModuleRelocation) == 48);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, entity) == 0);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, instantiation) == 8);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, symbol) == 12);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, addend) == 16);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, offset) == 24);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, source) == 28);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, label_block) == 32);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, aarch64) == 36);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, absolute) == 37);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, label_address) == 38);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, is_thread_local) == 39);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, thread_local_low) == 40);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, thread_local_index) == 41);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, kind) == 42);

typedef struct CodegenModuleDataRelocation CodegenModuleDataRelocation;
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
    // Instructions from the target-fixed 512-bit vocabulary. A kernel that
    // silently fell back to the scalar path reports zero here, which is the
    // only way a test can tell the two apart from the outside.
    u64 simd_operation_count;
    u32 function_count;
    u32 maximum_stack_frame_bytes;
    // Functions a non-NONE register-allocator mode handed to the canonical
    // stack path because machine selection does not cover them yet. Zero
    // under NONE; equal to the lowered function count until the machine
    // selector lands.
    u32 fallback_function_count;
    u32 reserved;
    // Census of why machine selection rejected each fallback function,
    // keyed by the first unsupported IR opcode; the final bucket counts
    // rejections with no specific opcode (capacity, verifier, targets).
    u32 fallback_opcode_counts[IR_OPCODE_COUNT + 1];
    // Fallbacks past selection: rows that failed the structural verifier,
    // placements over the guard-page-probe frame limit, and encodings that
    // did not fit or could not describe their unwind.
    u32 fallback_verify_count;
    u32 fallback_placement_count;
    u32 fallback_encode_count;
    // Allocator traffic summed over the functions the machine path
    // emitted: slot reloads, slot spills, and register-to-register moves
    // the placement inserted. Zero under NONE.
    u64 allocator_reload_count;
    u64 allocator_spill_count;
    u64 allocator_copy_count;
    u64 allocator_boundary_spill_count;
    u64 allocator_boundary_reload_count;
    u64 allocator_boundary_copy_count;
    u64 allocator_rematerialize_count;
    u64 allocator_pinned_register_count;
    u64 allocator_split_register_count;
    // Stage-9 scheduling: functions where the pass moved at least one row,
    // and the subset whose scheduled placement modeled cheaper and shipped.
    u64 allocator_scheduled_function_count;
    u64 allocator_schedule_kept_count;
};

struct CodegenModule
{
    IrModule* ir_module;
    ByteSlice code;
    ByteSlice read_only_data;
    ByteSlice writable_data;
    u64 zero_fill_size;
    ByteSlice thread_local_data;
    u64 thread_local_zero_size;
    CodegenModuleEntry* entries;
    CodegenFunctionDescriptor* functions;
    CodegenModuleGlobal* globals;
    CodegenModuleRelocation* relocations;
    CodegenModuleDataRelocation* data_relocations;
    CodegenLineEntry* line_entries;
    DebugLocationSeed* debug_locations;
    u32 line_entry_count;
    u32 debug_location_count;
    bool debug_info;
    u8 reserved[3];
    CodegenError error;
    CodegenAbi abi;
    CodegenStatistics statistics;
    IrFunctionId failed_function;
    IrInstructionId failed_instruction;
    IrOpcode failed_opcode;
    u32 entry_count;
    u32 function_count;
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

// Register-allocation strategy for the machine-IR backend path. `NONE` is
// the existing canonical direct emitter and stays the production default
// during migration. `MIR_STACK` places every eligible value in a stack
// location through the machine selector/encoder for differential testing.
// `FAST` minimizes allocation latency; `QUALITY` maximizes generated-code
// performance under a compile-time budget. Until the machine selector
// lands, every non-NONE mode falls back to the canonical path per function
// and the fallback is counted in CodegenStatistics.
typedef enum CodegenRegisterAllocatorMode
{
    CODEGEN_REGISTER_ALLOCATOR_NONE,
    CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
    CODEGEN_REGISTER_ALLOCATOR_FAST,
    CODEGEN_REGISTER_ALLOCATOR_QUALITY,
    CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT,
} CodegenRegisterAllocatorMode;

typedef struct CodegenModuleOptions CodegenModuleOptions;
struct CodegenModuleOptions
{
    // Zero selects the bounded host default. Tests and deterministic callers
    // may request a maximum width; one keeps the identical serial path.
    u32 lane_count;
    bool debug_info;
    bool assume_validated;
    // A CodegenRegisterAllocatorMode value; u8 storage keeps the options
    // record at its existing size.
    u8 register_allocator;
    u8 reserved[1];
};

// Fills the per-abi target cache on the calling thread. Call before lane_run;
// the cache is read without synchronization, so a gang that reaches it
// unwarmed reports through BUSTER_CHECK_SERIAL_INITIALIZATION instead of
// racing. compiler_prewarm() covers this along with the rest of the compiler.
BUSTER_F_DECL void codegen_prewarm(void);
BUSTER_F_DECL bool codegen_module_relocation_kind_valid(u8 kind);
BUSTER_F_DECL bool codegen_module_relocation_valid(CodegenModuleRelocation* relocation);
BUSTER_F_DECL String8 codegen_register_allocator_mode_string(CodegenRegisterAllocatorMode mode);
BUSTER_F_DECL CodegenAbi codegen_abi_for_target(Target target);
BUSTER_F_DECL CodegenAbiSignature codegen_classify_signature(Arena* arena, AnalysisResult* analysis, AnalysisTypeId function_type, CodegenAbi abi);
BUSTER_F_DECL CodegenFunction codegen_generate_function(Arena* arena, AnalysisResult* analysis, IrFunction* function, Target target);
BUSTER_F_DECL CodegenModule codegen_generate_module(Arena* arena, AnalysisResult* analysis, IrModule* module, Target target, CodegenModuleOptions options);
BUSTER_F_DECL CodegenModule codegen_generate_canonical_module(Arena* arena, IrProgram* program, IrModule* module, Target target, CodegenModuleOptions options);
BUSTER_F_DECL CodegenExecutable codegen_make_executable(CodegenFunction function);
BUSTER_F_DECL void codegen_release_executable(CodegenExecutable executable);
