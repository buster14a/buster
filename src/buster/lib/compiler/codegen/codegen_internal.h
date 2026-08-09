#pragma once

#include <buster/lib/compiler/codegen/codegen.h>

typedef struct CodegenBuffer CodegenBuffer;
struct CodegenBuffer
{
    u8* bytes;
    u64 count;
    u64 capacity;
    u8* value_registers;
    // Raised where a byte is refused for want of room, and nowhere else. Most
    // capacity failures are things a bigger buffer cannot fix -- a frame past
    // what a displacement can name, a reserve already at the limit of a u32
    // offset -- and they share `CODEGEN_ERROR_CAPACITY` with this one. Only the
    // module generator's code buffer carries the flag, and only so that it can
    // reserve cheaply and generate the module again when the estimate is short.
    bool* exhausted;
    u8 allocated_register_base;
    CodegenError error;
};

typedef struct CodegenRelocation CodegenRelocation;
struct CodegenRelocation
{
    CodegenRelocation* next;
    IrBlockId target;
    u32 displacement_offset;
};

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

typedef struct X64Builder X64Builder;
struct X64Builder
{
    Arena* arena;
    AnalysisResult* analysis;
    IrFunction* function;
    CodegenBuffer buffer;
    CodegenRelocation* first_relocation;
    CodegenRelocation* last_relocation;
    CodegenCallRelocation* first_call_relocation;
    CodegenCallRelocation* last_call_relocation;
    u32* block_offsets;
    u32 frame_size;
    u32 temporary_base;
    u32 temporary_count;
    u32 local_storage_base;
    u32* value_storage_offsets;
    u32* local_storage_offsets;
    u8* value_registers;
    u8* vector_registers;
    s32 hidden_result_displacement;
    s32 va_register_save_displacement;
    CodegenAbiSignature signature;
    CodegenAbi abi;
    Target target;
    CodegenBuffer read_only_data;
    CodegenDataRelocation* first_data_relocation;
    CodegenDataRelocation* last_data_relocation;
    u32 native_vector_operation_count;
    u32 split_vector_operation_count;
    u32 vzeroupper_count;
    u32 forwarded_wide_vector_load_count;
    bool upper_vector_dirty;
    IrValueId last_wide_vector_result;
    u32 last_wide_vector_size;
};

typedef IrAbiPart CodegenCanonicalAbiPart;
typedef IrAbiValue CodegenCanonicalAbiValue;

// What the stack pointer is worth on entry to a body and at every call, and so
// the alignment an outgoing-argument area gets for free.
#define CODEGEN_X64_STACK_ALIGNMENT 16

typedef struct CodegenCanonicalCallArgument CodegenCanonicalCallArgument;
struct CodegenCanonicalCallArgument
{
    CodegenCanonicalAbiValue abi;
    IrType* type;
    // How many registers the argument occupies when it is passed in them.
    u32 part_count;
    // How many eightbytes it occupies when it is passed on the stack. The two
    // differ for a wide vector: one register holds it, but the stack copy is
    // still its whole size.
    u32 stack_part_count;
    // Where it starts within the outgoing argument area, which is its own
    // alignment rounded up from where the argument before it ended and so not
    // simply the sum of the earlier arguments' sizes.
    u32 stack_offset;
    u32 copy_offset;
    u32 copy_size;
    u32 copy_alignment;
    u8 float_register;
    bool aggregate;
    bool on_stack;
    bool windows_indirect;
    bool system_v_aggregate;
};

typedef struct CodegenCanonicalCallLayout CodegenCanonicalCallLayout;
struct CodegenCanonicalCallLayout
{
    CodegenCanonicalCallArgument* arguments;
    CodegenCanonicalAbiValue return_abi;
    u32 argument_count;
    u32 stack_part_count;
    // What the outgoing argument area's base has to be aligned to: sixteen,
    // which the stack pointer is worth anyway, unless a stack argument wants
    // more -- a 256- or 512-bit vector, or an over-aligned aggregate.
    u32 stack_alignment;
    u32 windows_stack_size;
    u32 windows_copy_storage_size;
    u32 simulated_registers;
    u32 simulated_float_registers;
    bool stack_padding;
    bool indirect_return;
    bool windows_indirect_return;
};

BUSTER_F_DECL CodegenAbiSignature codegen_classify_signature_with_arguments(Arena* arena, AnalysisResult* analysis,
                                                                                  AnalysisTypeId function_type_id, AnalysisTypeId* argument_types,
                                                                                  u32 argument_count, Target target);
BUSTER_F_DECL CodegenError codegen_x64_maximum_call_stack_size(Arena* arena, AnalysisResult* analysis, IrFunction* function, Target target,
                                                                     u32* stack_size);
BUSTER_F_DECL CodegenError codegen_canonical_x64_call_layout(Arena* arena, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                                                    CodegenAbi abi, Target target, CodegenCanonicalCallLayout* layout);
BUSTER_F_DECL u32 codegen_canonical_x64_stack_argument_alignment(IrType* type);
BUSTER_F_DECL Target codegen_target_for_abi(CodegenAbi abi);
BUSTER_F_DECL Arena* codegen_worker_arena_create(u64 reserved_size, u64 granularity);
BUSTER_F_DECL void codegen_record_line(CodegenLineEntry* entries, u32* count, u32 capacity, u32 code_offset, u32 source, u32 line, u32 column);
BUSTER_F_DECL s32 codegen_debug_frame_offset(u32 offset, Target target, bool negative_offsets, u32 frame_size);
BUSTER_F_DECL bool x64_target_supports_native_vector(Target target, u64 size, u32 element_width, bool integer_operation);
BUSTER_F_DECL void x64_emit_vector_native_memory(X64Builder* builder, bool store, u32 size, X64Register base);
BUSTER_F_DECL void x64_emit_vector_native_binary_operation(X64Builder* builder, u8 prefix, u8 opcode, u32 size, X64Register base);
BUSTER_F_DECL void x64_emit_vzeroupper(X64Builder* builder);
BUSTER_F_DECL void codegen_canonical_x64_adjust_stack(CodegenBuffer* buffer, u32 byte_count, bool subtract);
BUSTER_F_DECL void codegen_canonical_a64_adjust_stack(CodegenBuffer* buffer, u32 byte_count, bool subtract);
BUSTER_F_DECL void codegen_canonical_a64_base_address(CodegenBuffer* buffer, u32 register_number, u32 base_register, u32 byte_offset);
BUSTER_F_DECL bool codegen_canonical_a64_frame_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                      bool sign_extend);
BUSTER_F_DECL u32 codegen_canonical_a64_remainder_divide_instruction(bool signed_remainder, bool wide);
BUSTER_F_DECL void a64_emit_load_pointer_offset(CodegenBuffer* buffer, u32 target, u32 address, u32 offset, u32 size);
BUSTER_F_DECL void a64_emit_store_pointer_offset(CodegenBuffer* buffer, u32 source, u32 address, u32 offset, u32 size);
BUSTER_F_DECL void a64_emit_initialize_aggregate_result(CodegenBuffer* buffer, u32* value_storage_offsets, IrValueId value);
BUSTER_F_DECL void a64_emit_copy_memory_registers(CodegenBuffer* buffer, u32 destination, u32 source, u32 scratch, u32 size);
BUSTER_F_DECL void a64_emit_float_load_offset(CodegenBuffer* buffer, u32 target, u32 offset, u32 size);
BUSTER_F_DECL void a64_emit_float_store_offset(CodegenBuffer* buffer, u32 source, u32 offset, u32 size);
#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL void codegen_test_emit_scalar(CodegenBuffer* buffer, u32 byte_count, u64 value);
#endif
