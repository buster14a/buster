#pragma once

#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/codegen/codegen.h>

typedef struct MachineFunction MachineFunction;
typedef struct MachineStackPlacement MachineStackPlacement;

typedef struct CodegenBuffer CodegenBuffer;
struct CodegenBuffer
{
    u8* bytes;
    u64 count;
    u64 capacity;
    void* x64_metadata_cache;
    // Raised where a byte is refused for want of room, and nowhere else. Most
    // capacity failures are things a bigger buffer cannot fix -- a frame past
    // what a displacement can name, a reserve already at the limit of a u32
    // offset -- and they share `CODEGEN_ERROR_CAPACITY` with this one. Only the
    // module generator's code buffer carries the flag, and only so that it can
    // reserve cheaply and generate the module again when the estimate is short.
    bool* exhausted;
    CodegenError error;
};

// Shared bounded AArch64 Windows probe. Returns false for a small or
// misaligned request; a handled request reports emission errors in buffer.
BUSTER_F_DECL bool codegen_a64_windows_large_stack_adjust(CodegenBuffer* buffer, u32 size, bool subtract,
                                                         CodegenFunctionDescriptor* descriptor, u32 action_capacity);
BUSTER_F_DECL u32 codegen_a64_windows_save_area_size(u32 saved_register_count);

typedef enum X64Register
{
    X64_REGISTER_RAX,
    X64_REGISTER_RCX,
    X64_REGISTER_RDX,
    X64_REGISTER_RBX,
    X64_REGISTER_RSP,
    X64_REGISTER_RBP,
    X64_REGISTER_RSI,
    X64_REGISTER_RDI,
    X64_REGISTER_R8,
    X64_REGISTER_R9,
    X64_REGISTER_R10,
    X64_REGISTER_R11,
    X64_REGISTER_R12,
    X64_REGISTER_R13,
    X64_REGISTER_R14,
    X64_REGISTER_R15,
} X64Register;

typedef IrAbiPart CodegenCanonicalAbiPart;
typedef IrAbiValue CodegenCanonicalAbiValue;

// Shared inline-assembly planning services. Machine selection uses these to
// close a template over explicit physical registers before the shared
// assembler encodes it; none of them emit canonical instructions.
BUSTER_F_DECL bool codegen_inline_assembly_resolve_template(Arena* arena, IrProgram* program, IrFunction* function,
                                                            IrInstruction* instruction, IrInstructionExtra extra,
                                                            X64Register* registers, u32* vector_registers,
                                                            AssemblySyntax syntax, String8* source_out, String8* reason_out);
BUSTER_F_DECL bool codegen_inline_assembly_clobber_register(String8 clobber, X64Register* register_out);
BUSTER_F_DECL bool codegen_inline_assembly_clobber_vector_register(String8 clobber, u32* register_out);
BUSTER_F_DECL bool codegen_inline_assembly_constraint_register(u64 constraint, X64Register* register_out);
BUSTER_F_DECL IrSymbolId codegen_global_assembly_symbol(IrProgram* program, String8 name, Target target, IrSymbolKind kind);
BUSTER_F_DECL bool codegen_assembly_durable_name(String8 durable, String8* name);
BUSTER_F_DECL bool codegen_global_assembly_apply_symbol_directive(IrProgram* program, Target target, String8 line,
                                                                  String8 durable_names, bool* recognized);

// What the stack pointer is worth on entry to a body and at every call, and so
// the alignment an outgoing-argument area gets for free.
#define CODEGEN_X64_STACK_ALIGNMENT 16

// The guard-page interval stack growth must touch: chunked RSP adjustments,
// probe loops, and the unwind-action capacity that counts those chunks all
// derive from this one value.
#define CODEGEN_X64_STACK_PROBE_PAGE 4096u

BUSTER_F_DECL bool codegen_canonical_x64_type_is_f80(IrType* type);
BUSTER_F_DECL bool codegen_canonical_x64_type_is_f80_x87_shape(IrProgram* program, IrTypeId type_id);
BUSTER_F_DECL bool codegen_canonical_x64_type_contains_f80(IrProgram* program, IrTypeId type_id);
BUSTER_F_DECL bool codegen_canonical_x64_abi_is_f80_result(IrType* type, CodegenCanonicalAbiValue const* abi);
BUSTER_F_DECL void codegen_canonical_x64_x87_memory(CodegenBuffer* buffer, bool store, X64Register base, s32 displacement);
BUSTER_F_DECL void codegen_canonical_x64_zero_f80_padding(CodegenBuffer* buffer, X64Register base, s32 displacement);
BUSTER_F_DECL bool codegen_canonical_x64_emit_f80_copy(CodegenBuffer* buffer, X64Register source_base, s32 source_displacement,
                                                            X64Register destination_base, s32 destination_displacement, u32* x87_depth);
BUSTER_F_DECL bool codegen_canonical_x64_emit_f80_store_top(CodegenBuffer* buffer, X64Register destination_base, s32 destination_displacement,
                                                                 u32* x87_depth);
BUSTER_F_DECL bool codegen_canonical_x64_store_f80_constant(CodegenBuffer* buffer, s32 displacement, u64 significand, u16 sign_exponent);
BUSTER_F_DECL Target codegen_target_for_abi(CodegenAbi abi);
BUSTER_F_DECL void codegen_record_line(CodegenLineEntry* entries, u32* count, u32 capacity, u32 code_offset, u32 source, u32 line, u32 column);
BUSTER_F_DECL s32 codegen_debug_frame_offset(u32 offset, Target target, bool negative_offsets, u32 frame_size);
// Shared Win64 large-frame probe. R10/R11 walk the stack without changing
// RSP; the final instruction reserves the complete frame in one action.
BUSTER_F_DECL bool codegen_x64_emit_windows_stack_allocate(CodegenBuffer* buffer, u32 size, CodegenFunctionDescriptor* descriptor,
                                                          u32 action_capacity, u32 function_offset);
BUSTER_F_DECL void codegen_canonical_a64_base_address(CodegenBuffer* buffer, u32 register_number, u32 base_register, u32 byte_offset);
BUSTER_F_DECL bool codegen_canonical_a64_frame_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                      bool sign_extend);
BUSTER_F_DECL u32 codegen_canonical_a64_remainder_divide_instruction(bool signed_remainder, bool wide);
BUSTER_F_DECL void a64_emit_load_pointer_offset(CodegenBuffer* buffer, u32 target, u32 address, u32 offset, u32 size);
BUSTER_F_DECL void a64_emit_store_pointer_offset(CodegenBuffer* buffer, u32 source, u32 address, u32 offset, u32 size);
#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL void codegen_test_emit_scalar(CodegenBuffer* buffer, u32 byte_count, u64 value);
BUSTER_F_DECL bool codegen_test_record_machine_locations(Arena* arena, CodegenModule* result, u32 capacity, IrFunction* ir_function,
                                                          MachineFunction const* function, MachineStackPlacement const* placement,
                                                          u32 const* row_offsets, u32 function_start, u32 function_end, u32 frame_base_offset,
                                                          Target target);
// Recording into arena-owned seed storage, which grows with what it emits;
// `capacity` carries the starting capacity in and the final one out.
BUSTER_F_DECL bool codegen_test_record_machine_locations_growing(Arena* arena, CodegenModule* result, u32* capacity, IrFunction* ir_function,
                                                                  MachineFunction const* function, MachineStackPlacement const* placement,
                                                                  u32 const* row_offsets, u32 function_start, u32 function_end,
                                                                  u32 frame_base_offset, Target target);
// The widest change-point timeline the event-driven recording builds for a
// function, so a test can assert the timelines stay sparse.
BUSTER_F_DECL u32 codegen_test_machine_debug_widest_timeline(Arena* arena, MachineFunction const* function,
                                                              MachineStackPlacement const* placement, u32 frame_base_offset, Target target);
// Whole-function reference recording, for differential comparison against the
// event-driven routine the compiler actually runs.
BUSTER_F_DECL bool codegen_test_record_machine_locations_dense(Arena* arena, CodegenModule* result, u32 capacity, IrFunction* ir_function,
                                                                MachineFunction const* function, MachineStackPlacement const* placement,
                                                                u32 const* row_offsets, u32 function_start, u32 function_end,
                                                                u32 frame_base_offset, Target target);
#endif
