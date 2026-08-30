#pragma once

// Public API of native code generation. The one producing entry point is
// codegen_generate_canonical_module: canonical IR in, a CodegenModule of
// code bytes, data images, relocations, unwind actions, and statistics out,
// ready for the object writer or the JIT. Unsupported shapes come back as a
// CodegenError naming the failing instruction — never as silently wrong
// code. Call codegen_prewarm (or codegen_prewarm_for_target) on one thread
// before generating from parallel lanes.

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
    CODEGEN_ABI_MAX_PARTS = IR_ABI_MAX_PARTS,
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

// Hundreds of thousands of rows per compile, so the record is 12 bytes:
// source and column are u16, saturated by codegen_record_line (a compile
// with 64K+ files degrades to file 0, and no consumer distinguishes
// columns past 64K).
typedef struct CodegenLineEntry CodegenLineEntry;
struct CodegenLineEntry
{
    u32 code_offset;
    u32 line;
    u16 source;
    u16 column;
};

BUSTER_CT_CHECK(sizeof(CodegenLineEntry) == 12);

struct CodegenFunction
{
    ByteSlice code;
    ByteSlice read_only_data;
    CodegenFunctionDescriptor descriptor;
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
    // ELF initial-exec: the loader stores the thread-pointer offset in a GOT
    // slot and the code adds that slot to the thread pointer, so the offset
    // does not have to be known when the referencing object is built.
    CODEGEN_MODULE_RELOCATION_X86_64_GOTTPOFF,
    // ELF general-dynamic: the lea that hands __tls_get_addr the address of
    // the module/offset pair the loader fills in, and the call to it.  The
    // pair is a linker-built GOT entry (R_X86_64_DTPMOD64 plus
    // R_X86_64_DTPOFF64); the object carries only these two sites.  The call
    // resolves to __tls_get_addr rather than to the relocation's own symbol,
    // which is why it is a kind of its own rather than a plain PC32.
    CODEGEN_MODULE_RELOCATION_X86_64_TLSGD,
    CODEGEN_MODULE_RELOCATION_X86_64_TLS_GET_ADDR_PLT32,
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
    // The position-independent code model's two forms, both x86-64 ELF and
    // both rip-relative like PC32. GOTPCREL patches the displacement of a
    // load whose result is the symbol's address, read out of the slot the
    // linker reserves for it, so an interposing definition is the one every
    // reference sees. PLT32 patches a direct call's rel32 and tells the
    // linker it may route that call through a procedure linkage entry --
    // which is what makes a call to an interposable function placeable in a
    // shared object at all.
    CODEGEN_MODULE_RELOCATION_X86_64_GOTPCREL,
    CODEGEN_MODULE_RELOCATION_X86_64_PLT32,
    CODEGEN_MODULE_RELOCATION_COUNT,
} CodegenModuleRelocationKind;

struct CodegenModuleRelocation
{
    // The wide member leads so the record packs to 32 bytes; `source` is
    // stored as u8 for the same reason (the enum has four values).
    s64 addend;
    IrSymbolId symbol;
    u32 offset;
    IrBlockId label_block;
    u8 source;
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
BUSTER_CT_CHECK((u32)CODEGEN_MODULE_RELOCATION_SOURCE_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK(BUSTER_ALIGN_OF(CodegenModuleRelocation) == 8);
BUSTER_CT_CHECK(sizeof(CodegenModuleRelocation) == 32);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, addend) == 0);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, symbol) == 8);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, offset) == 12);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, label_block) == 16);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, source) == 20);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, aarch64) == 21);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, absolute) == 22);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, label_address) == 23);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, is_thread_local) == 24);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, thread_local_low) == 25);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, thread_local_index) == 26);
BUSTER_CT_CHECK(BUSTER_OFFSET_OF(CodegenModuleRelocation, kind) == 27);

typedef struct CodegenModuleDataRelocation CodegenModuleDataRelocation;
struct CodegenModuleDataRelocation
{
    u32 code_offset;
    u32 data_offset;
    CodegenDataRelocationKind kind;
};

// A zero-fill global at or past this size is laid out after every smaller one.
// Every image layout places the zero-fill section beyond code, so offsets taken
// purely in declaration order let a ~2GiB array push each global declared after
// it past RIP-relative +/-2^31 reach, failing the link with
// LINK_ERROR_RELOCATION for a program clang links. Small-first keeps every
// small global and every large array's base in range; array interiors are
// reached through a register either way. The value matches clang's
// -mlarge-data-threshold default.
#define CODEGEN_LARGE_ZERO_FILL_THRESHOLD (64u * 1024)

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
    // Canonical emission reports its planned value/frame storage separately.
    // Machine emission has no canonical value-slot plan and reports the
    // retained placement frame in both counters.
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
    // Exact-form machine encoder telemetry. These remain append-only so
    // existing statistics consumers retain their layout and meaning.
    u64 exact_attempts;
    u64 exact_successes;
    u64 exact_failures;
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
    // -fPIC as this module was generated under it, carried past code
    // generation because the object writer has one decision of its own to
    // make from it: which symbol an unwind record's function pointer is
    // relocated against.
    bool position_independent;
    u8 reserved[2];
    CodegenError error;
    CodegenAbi abi;
    CodegenStatistics statistics;
    IrFunctionId failed_function;
    IrInstructionId failed_instruction;
    IrOpcode failed_opcode;
    // Where a module-level assembly block failed. `failed_assembly` indexes
    // IrModule.assemblies and `failed_assembly_line` is the one-based line
    // inside that block's own text, both meaningful only while
    // `failed_in_assembly`; `failed_function` and `failed_instruction` mean
    // nothing then, which is what used to make the diagnostic blame the next
    // C function in the file. The block's text is a concatenation of string
    // literal contents and so has no offset in the file it was written in:
    // IrModuleAssembly.source_range is what names that position.
    u32 failed_assembly;
    u32 failed_assembly_line;
    bool failed_in_assembly;
    u8 reserved_failure[3];
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

// Register-allocation strategy for the machine-IR backend path. `NONE` uses
// the canonical direct emitter and is the explicit compatibility/diagnostic
// escape hatch. `MIR_STACK` places every eligible value in a stack location
// through the machine selector/encoder for differential testing. `FAST` is
// the driver default and minimizes allocation latency; `QUALITY` maximizes
// generated-code performance under a compile-time budget. Every non-NONE
// mode falls back to the canonical path per unsupported function, and the
// fallback is counted in CodegenStatistics.
typedef enum CodegenRegisterAllocatorMode
{
    CODEGEN_REGISTER_ALLOCATOR_NONE,
    CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
    CODEGEN_REGISTER_ALLOCATOR_FAST,
    CODEGEN_REGISTER_ALLOCATOR_QUALITY,
    CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT,
} CodegenRegisterAllocatorMode;

// Which thread-local addressing sequence a symbol reference is lowered to.
// The three are not interchangeable: local-exec folds a constant offset from
// the thread pointer and is only correct for the main executable's own block,
// initial-exec reads the offset out of a GOT slot the loader fills and is
// correct for anything present at program start, and general-dynamic asks
// __tls_get_addr at run time and is the only one correct for a module that
// may be dlopened.  Each is strictly more general and strictly slower than
// the one before it, so the choice is the narrowest form that can be right.
typedef enum CodegenThreadLocalModel
{
    CODEGEN_THREAD_LOCAL_LOCAL_EXEC,
    CODEGEN_THREAD_LOCAL_INITIAL_EXEC,
    CODEGEN_THREAD_LOCAL_GENERAL_DYNAMIC,
} CodegenThreadLocalModel;

typedef struct CodegenModuleOptions CodegenModuleOptions;
struct CodegenModuleOptions
{
    bool debug_info;
    bool assume_validated;
    // -fPIC/-fpic: this object may end up in a shared library. No
    // thread-local definition it names can be assumed to sit in the initial
    // thread-local block, and a symbol another object could interpose is
    // addressed through its GOT slot rather than rip-relative and called
    // through its procedure linkage entry. The second half is honored on
    // x86-64 ELF, where those are the references `ld -shared` refuses; every
    // other target's address materialization is a different one and this flag
    // does not reach it.
    bool position_independent;
    // A CodegenRegisterAllocatorMode value; u8 storage keeps the options
    // record at its existing size.
    u8 register_allocator;
    // An AssemblySyntax value.  Keep this byte-sized so the public options
    // record remains ABI-compatible with callers that embed it.
    u8 assembly_syntax;
};

// Fills the per-abi target cache. Emission reads that cache without ever
// filling it, so an unwarmed entry reached during emission reports through
// BUSTER_CHECK_SERIAL_INITIALIZATION rather than being built mid-module.
// Target-specific x86 metadata and exact machine plans are filled by
// codegen_prewarm_for_target() before the module is generated.
BUSTER_F_DECL void codegen_prewarm(void);
BUSTER_F_DECL void codegen_prewarm_for_target(Target target);
BUSTER_F_DECL bool codegen_module_relocation_kind_valid(u8 kind);
BUSTER_F_DECL bool codegen_module_relocation_valid(CodegenModuleRelocation* relocation);
// The canonical named-parameter classification the a64 variadic model is
// defined over; the AArch64 machine selector's VA_START mirrors the
// canonical emitter's simulation through this exact walk.
BUSTER_F_DECL bool codegen_canonical_integer_aggregate_parts(IrProgram* program, IrTypeId type_id, u32* part_count);
BUSTER_F_DECL String8 codegen_register_allocator_mode_string(CodegenRegisterAllocatorMode mode);
// The ELF thread-local model for one symbol reference.  Windows and Mach-O
// have their own sequences and never ask.
BUSTER_F_DECL CodegenThreadLocalModel codegen_thread_local_model(bool position_independent, bool symbol_is_definition);
BUSTER_F_DECL CodegenAbi codegen_abi_for_target(Target target);
BUSTER_F_DECL CodegenModule codegen_generate_canonical_module(Arena* arena, IrProgram* program, IrModule* module, Target target, CodegenModuleOptions options);
BUSTER_F_DECL CodegenExecutable codegen_make_executable(CodegenFunction function);
BUSTER_F_DECL void codegen_release_executable(CodegenExecutable executable);
