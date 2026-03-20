#pragma once
#include <buster/compiler/backend/code_generation.h>
#include <buster/compiler/backend/instruction_selection.h>
#include <buster/arena.h>
#include <buster/integer.h>
#include <buster/assertion.h>
#include <buster/file.h>

#include <buster/simd.h>
// #include <buster/x86_64_instructions.c>
ENUM(X86RegisterName,
    X86_REGISTER_RAX = 0,
    X86_REGISTER_EAX = 1,
    X86_REGISTER_AX = 2,
    X86_REGISTER_AH = 3,
    X86_REGISTER_AL = 4,
    X86_REGISTER_RCX = 5,
    X86_REGISTER_ECX = 6,
    X86_REGISTER_CX = 7,
    X86_REGISTER_CH = 8,
    X86_REGISTER_CL = 9,
    X86_REGISTER_RDX = 10,
    X86_REGISTER_EDX = 11,
    X86_REGISTER_DX = 12,
    X86_REGISTER_DH = 13,
    X86_REGISTER_DL = 14,
    X86_REGISTER_RBX = 15,
    X86_REGISTER_EBX = 16,
    X86_REGISTER_BX = 17,
    X86_REGISTER_BH = 18,
    X86_REGISTER_BL = 19,
    X86_REGISTER_RSP = 20,
    X86_REGISTER_ESP = 21,
    X86_REGISTER_SP = 22,
    X86_REGISTER_SPL = 23,
    X86_REGISTER_RBP = 24,
    X86_REGISTER_EBP = 25,
    X86_REGISTER_BP = 26,
    X86_REGISTER_BPL = 27,
    X86_REGISTER_RSI = 28,
    X86_REGISTER_ESI = 29,
    X86_REGISTER_SI = 30,
    X86_REGISTER_SIL = 31,
    X86_REGISTER_RDI = 32,
    X86_REGISTER_EDI = 33,
    X86_REGISTER_DI = 34,
    X86_REGISTER_DIL = 35,
    X86_REGISTER_R8 = 36,
    X86_REGISTER_R8D = 37,
    X86_REGISTER_R8W = 38,
    X86_REGISTER_R8B = 39,
    X86_REGISTER_R9 = 40,
    X86_REGISTER_R9D = 41,
    X86_REGISTER_R9W = 42,
    X86_REGISTER_R9B = 43,
    X86_REGISTER_R10 = 44,
    X86_REGISTER_R10D = 45,
    X86_REGISTER_R10W = 46,
    X86_REGISTER_R10B = 47,
    X86_REGISTER_R11 = 48,
    X86_REGISTER_R11D = 49,
    X86_REGISTER_R11W = 50,
    X86_REGISTER_R11B = 51,
    X86_REGISTER_R12 = 52,
    X86_REGISTER_R12D = 53,
    X86_REGISTER_R12W = 54,
    X86_REGISTER_R12B = 55,
    X86_REGISTER_R13 = 56,
    X86_REGISTER_R13D = 57,
    X86_REGISTER_R13W = 58,
    X86_REGISTER_R13B = 59,
    X86_REGISTER_R14 = 60,
    X86_REGISTER_R14D = 61,
    X86_REGISTER_R14W = 62,
    X86_REGISTER_R14B = 63,
    X86_REGISTER_R15 = 64,
    X86_REGISTER_R15D = 65,
    X86_REGISTER_R15W = 66,
    X86_REGISTER_R15B = 67,
    X86_REGISTER_R16 = 68,
    X86_REGISTER_R16D = 69,
    X86_REGISTER_R16W = 70,
    X86_REGISTER_R16B = 71,
    X86_REGISTER_R17 = 72,
    X86_REGISTER_R17D = 73,
    X86_REGISTER_R17W = 74,
    X86_REGISTER_R17B = 75,
    X86_REGISTER_R18 = 76,
    X86_REGISTER_R18D = 77,
    X86_REGISTER_R18W = 78,
    X86_REGISTER_R18B = 79,
    X86_REGISTER_R19 = 80,
    X86_REGISTER_R19D = 81,
    X86_REGISTER_R19W = 82,
    X86_REGISTER_R19B = 83,
    X86_REGISTER_R20 = 84,
    X86_REGISTER_R20D = 85,
    X86_REGISTER_R20W = 86,
    X86_REGISTER_R20B = 87,
    X86_REGISTER_R21 = 88,
    X86_REGISTER_R21D = 89,
    X86_REGISTER_R21W = 90,
    X86_REGISTER_R21B = 91,
    X86_REGISTER_R22 = 92,
    X86_REGISTER_R22D = 93,
    X86_REGISTER_R22W = 94,
    X86_REGISTER_R22B = 95,
    X86_REGISTER_R23 = 96,
    X86_REGISTER_R23D = 97,
    X86_REGISTER_R23W = 98,
    X86_REGISTER_R23B = 99,
    X86_REGISTER_R24 = 100,
    X86_REGISTER_R24D = 101,
    X86_REGISTER_R24W = 102,
    X86_REGISTER_R24B = 103,
    X86_REGISTER_R25 = 104,
    X86_REGISTER_R25D = 105,
    X86_REGISTER_R25W = 106,
    X86_REGISTER_R25B = 107,
    X86_REGISTER_R26 = 108,
    X86_REGISTER_R26D = 109,
    X86_REGISTER_R26W = 110,
    X86_REGISTER_R26B = 111,
    X86_REGISTER_R27 = 112,
    X86_REGISTER_R27D = 113,
    X86_REGISTER_R27W = 114,
    X86_REGISTER_R27B = 115,
    X86_REGISTER_R28 = 116,
    X86_REGISTER_R28D = 117,
    X86_REGISTER_R28W = 118,
    X86_REGISTER_R28B = 119,
    X86_REGISTER_R29 = 120,
    X86_REGISTER_R29D = 121,
    X86_REGISTER_R29W = 122,
    X86_REGISTER_R29B = 123,
    X86_REGISTER_R30 = 124,
    X86_REGISTER_R30D = 125,
    X86_REGISTER_R30W = 126,
    X86_REGISTER_R30B = 127,
    X86_REGISTER_R31 = 128,
    X86_REGISTER_R31D = 129,
    X86_REGISTER_R31W = 130,
    X86_REGISTER_R31B = 131,
    X86_REGISTER_RIP = 132,
    X86_REGISTER_EIP = 133,
    X86_REGISTER_IP = 134,
    X86_REGISTER_FLAGS = 135,
    X86_REGISTER_RFLAGS = 136,
    X86_REGISTER_EFLAGS = 137,
    X86_REGISTER_ES = 138,
    X86_REGISTER_CS = 139,
    X86_REGISTER_SS = 140,
    X86_REGISTER_DS = 141,
    X86_REGISTER_FS = 142,
    X86_REGISTER_GS = 143,
    X86_REGISTER_MMX0 = 144,
    X86_REGISTER_MMX1 = 145,
    X86_REGISTER_MMX2 = 146,
    X86_REGISTER_MMX3 = 147,
    X86_REGISTER_MMX4 = 148,
    X86_REGISTER_MMX5 = 149,
    X86_REGISTER_MMX6 = 150,
    X86_REGISTER_MMX7 = 151,
    X86_REGISTER_ST0 = 152,
    X86_REGISTER_ST1 = 153,
    X86_REGISTER_ST2 = 154,
    X86_REGISTER_ST3 = 155,
    X86_REGISTER_ST4 = 156,
    X86_REGISTER_ST5 = 157,
    X86_REGISTER_ST6 = 158,
    X86_REGISTER_ST7 = 159,
    X86_REGISTER_CR0 = 160,
    X86_REGISTER_CR1 = 161,
    X86_REGISTER_CR2 = 162,
    X86_REGISTER_CR3 = 163,
    X86_REGISTER_CR4 = 164,
    X86_REGISTER_CR5 = 165,
    X86_REGISTER_CR6 = 166,
    X86_REGISTER_CR7 = 167,
    X86_REGISTER_CR8 = 168,
    X86_REGISTER_CR9 = 169,
    X86_REGISTER_CR10 = 170,
    X86_REGISTER_CR11 = 171,
    X86_REGISTER_CR12 = 172,
    X86_REGISTER_CR13 = 173,
    X86_REGISTER_CR14 = 174,
    X86_REGISTER_CR15 = 175,
    X86_REGISTER_DR0 = 176,
    X86_REGISTER_DR1 = 177,
    X86_REGISTER_DR2 = 178,
    X86_REGISTER_DR3 = 179,
    X86_REGISTER_DR4 = 180,
    X86_REGISTER_DR5 = 181,
    X86_REGISTER_DR6 = 182,
    X86_REGISTER_DR7 = 183,
    X86_REGISTER_COUNT = 184,
);

BUSTER_GLOBAL_LOCAL String8 x86_register_name_strings[X86_REGISTER_COUNT] = {
    [X86_REGISTER_RAX] = S8("rax"),
    [X86_REGISTER_EAX] = S8("eax"),
    [X86_REGISTER_AX] = S8("ax"),
    [X86_REGISTER_AH] = S8("ah"),
    [X86_REGISTER_AL] = S8("al"),
    [X86_REGISTER_RCX] = S8("rcx"),
    [X86_REGISTER_ECX] = S8("ecx"),
    [X86_REGISTER_CX] = S8("cx"),
    [X86_REGISTER_CH] = S8("ch"),
    [X86_REGISTER_CL] = S8("cl"),
    [X86_REGISTER_RDX] = S8("rdx"),
    [X86_REGISTER_EDX] = S8("edx"),
    [X86_REGISTER_DX] = S8("dx"),
    [X86_REGISTER_DH] = S8("dh"),
    [X86_REGISTER_DL] = S8("dl"),
    [X86_REGISTER_RBX] = S8("rbx"),
    [X86_REGISTER_EBX] = S8("ebx"),
    [X86_REGISTER_BX] = S8("bx"),
    [X86_REGISTER_BH] = S8("bh"),
    [X86_REGISTER_BL] = S8("bl"),
    [X86_REGISTER_RSP] = S8("rsp"),
    [X86_REGISTER_ESP] = S8("esp"),
    [X86_REGISTER_SP] = S8("sp"),
    [X86_REGISTER_SPL] = S8("spl"),
    [X86_REGISTER_RBP] = S8("rbp"),
    [X86_REGISTER_EBP] = S8("ebp"),
    [X86_REGISTER_BP] = S8("bp"),
    [X86_REGISTER_BPL] = S8("bpl"),
    [X86_REGISTER_RSI] = S8("rsi"),
    [X86_REGISTER_ESI] = S8("esi"),
    [X86_REGISTER_SI] = S8("si"),
    [X86_REGISTER_SIL] = S8("sil"),
    [X86_REGISTER_RDI] = S8("rdi"),
    [X86_REGISTER_EDI] = S8("edi"),
    [X86_REGISTER_DI] = S8("di"),
    [X86_REGISTER_DIL] = S8("dil"),
    [X86_REGISTER_R8] = S8("r8"),
    [X86_REGISTER_R8D] = S8("r8d"),
    [X86_REGISTER_R8W] = S8("r8w"),
    [X86_REGISTER_R8B] = S8("r8b"),
    [X86_REGISTER_R9] = S8("r9"),
    [X86_REGISTER_R9D] = S8("r9d"),
    [X86_REGISTER_R9W] = S8("r9w"),
    [X86_REGISTER_R9B] = S8("r9b"),
    [X86_REGISTER_R10] = S8("r10"),
    [X86_REGISTER_R10D] = S8("r10d"),
    [X86_REGISTER_R10W] = S8("r10w"),
    [X86_REGISTER_R10B] = S8("r10b"),
    [X86_REGISTER_R11] = S8("r11"),
    [X86_REGISTER_R11D] = S8("r11d"),
    [X86_REGISTER_R11W] = S8("r11w"),
    [X86_REGISTER_R11B] = S8("r11b"),
    [X86_REGISTER_R12] = S8("r12"),
    [X86_REGISTER_R12D] = S8("r12d"),
    [X86_REGISTER_R12W] = S8("r12w"),
    [X86_REGISTER_R12B] = S8("r12b"),
    [X86_REGISTER_R13] = S8("r13"),
    [X86_REGISTER_R13D] = S8("r13d"),
    [X86_REGISTER_R13W] = S8("r13w"),
    [X86_REGISTER_R13B] = S8("r13b"),
    [X86_REGISTER_R14] = S8("r14"),
    [X86_REGISTER_R14D] = S8("r14d"),
    [X86_REGISTER_R14W] = S8("r14w"),
    [X86_REGISTER_R14B] = S8("r14b"),
    [X86_REGISTER_R15] = S8("r15"),
    [X86_REGISTER_R15D] = S8("r15d"),
    [X86_REGISTER_R15W] = S8("r15w"),
    [X86_REGISTER_R15B] = S8("r15b"),
    [X86_REGISTER_R16] = S8("r16"),
    [X86_REGISTER_R16D] = S8("r16d"),
    [X86_REGISTER_R16W] = S8("r16w"),
    [X86_REGISTER_R16B] = S8("r16b"),
    [X86_REGISTER_R17] = S8("r17"),
    [X86_REGISTER_R17D] = S8("r17d"),
    [X86_REGISTER_R17W] = S8("r17w"),
    [X86_REGISTER_R17B] = S8("r17b"),
    [X86_REGISTER_R18] = S8("r18"),
    [X86_REGISTER_R18D] = S8("r18d"),
    [X86_REGISTER_R18W] = S8("r18w"),
    [X86_REGISTER_R18B] = S8("r18b"),
    [X86_REGISTER_R19] = S8("r19"),
    [X86_REGISTER_R19D] = S8("r19d"),
    [X86_REGISTER_R19W] = S8("r19w"),
    [X86_REGISTER_R19B] = S8("r19b"),
    [X86_REGISTER_R20] = S8("r20"),
    [X86_REGISTER_R20D] = S8("r20d"),
    [X86_REGISTER_R20W] = S8("r20w"),
    [X86_REGISTER_R20B] = S8("r20b"),
    [X86_REGISTER_R21] = S8("r21"),
    [X86_REGISTER_R21D] = S8("r21d"),
    [X86_REGISTER_R21W] = S8("r21w"),
    [X86_REGISTER_R21B] = S8("r21b"),
    [X86_REGISTER_R22] = S8("r22"),
    [X86_REGISTER_R22D] = S8("r22d"),
    [X86_REGISTER_R22W] = S8("r22w"),
    [X86_REGISTER_R22B] = S8("r22b"),
    [X86_REGISTER_R23] = S8("r23"),
    [X86_REGISTER_R23D] = S8("r23d"),
    [X86_REGISTER_R23W] = S8("r23w"),
    [X86_REGISTER_R23B] = S8("r23b"),
    [X86_REGISTER_R24] = S8("r24"),
    [X86_REGISTER_R24D] = S8("r24d"),
    [X86_REGISTER_R24W] = S8("r24w"),
    [X86_REGISTER_R24B] = S8("r24b"),
    [X86_REGISTER_R25] = S8("r25"),
    [X86_REGISTER_R25D] = S8("r25d"),
    [X86_REGISTER_R25W] = S8("r25w"),
    [X86_REGISTER_R25B] = S8("r25b"),
    [X86_REGISTER_R26] = S8("r26"),
    [X86_REGISTER_R26D] = S8("r26d"),
    [X86_REGISTER_R26W] = S8("r26w"),
    [X86_REGISTER_R26B] = S8("r26b"),
    [X86_REGISTER_R27] = S8("r27"),
    [X86_REGISTER_R27D] = S8("r27d"),
    [X86_REGISTER_R27W] = S8("r27w"),
    [X86_REGISTER_R27B] = S8("r27b"),
    [X86_REGISTER_R28] = S8("r28"),
    [X86_REGISTER_R28D] = S8("r28d"),
    [X86_REGISTER_R28W] = S8("r28w"),
    [X86_REGISTER_R28B] = S8("r28b"),
    [X86_REGISTER_R29] = S8("r29"),
    [X86_REGISTER_R29D] = S8("r29d"),
    [X86_REGISTER_R29W] = S8("r29w"),
    [X86_REGISTER_R29B] = S8("r29b"),
    [X86_REGISTER_R30] = S8("r30"),
    [X86_REGISTER_R30D] = S8("r30d"),
    [X86_REGISTER_R30W] = S8("r30w"),
    [X86_REGISTER_R30B] = S8("r30b"),
    [X86_REGISTER_R31] = S8("r31"),
    [X86_REGISTER_R31D] = S8("r31d"),
    [X86_REGISTER_R31W] = S8("r31w"),
    [X86_REGISTER_R31B] = S8("r31b"),
    [X86_REGISTER_RIP] = S8("rip"),
    [X86_REGISTER_EIP] = S8("eip"),
    [X86_REGISTER_IP] = S8("ip"),
    [X86_REGISTER_FLAGS] = S8("flags"),
    [X86_REGISTER_RFLAGS] = S8("rflags"),
    [X86_REGISTER_EFLAGS] = S8("eflags"),
    [X86_REGISTER_ES] = S8("es"),
    [X86_REGISTER_CS] = S8("cs"),
    [X86_REGISTER_SS] = S8("ss"),
    [X86_REGISTER_DS] = S8("ds"),
    [X86_REGISTER_FS] = S8("fs"),
    [X86_REGISTER_GS] = S8("gs"),
    [X86_REGISTER_MMX0] = S8("mmx0"),
    [X86_REGISTER_MMX1] = S8("mmx1"),
    [X86_REGISTER_MMX2] = S8("mmx2"),
    [X86_REGISTER_MMX3] = S8("mmx3"),
    [X86_REGISTER_MMX4] = S8("mmx4"),
    [X86_REGISTER_MMX5] = S8("mmx5"),
    [X86_REGISTER_MMX6] = S8("mmx6"),
    [X86_REGISTER_MMX7] = S8("mmx7"),
    [X86_REGISTER_ST0] = S8("st0"),
    [X86_REGISTER_ST1] = S8("st1"),
    [X86_REGISTER_ST2] = S8("st2"),
    [X86_REGISTER_ST3] = S8("st3"),
    [X86_REGISTER_ST4] = S8("st4"),
    [X86_REGISTER_ST5] = S8("st5"),
    [X86_REGISTER_ST6] = S8("st6"),
    [X86_REGISTER_ST7] = S8("st7"),
    [X86_REGISTER_CR0] = S8("cr0"),
    [X86_REGISTER_CR1] = S8("cr1"),
    [X86_REGISTER_CR2] = S8("cr2"),
    [X86_REGISTER_CR3] = S8("cr3"),
    [X86_REGISTER_CR4] = S8("cr4"),
    [X86_REGISTER_CR5] = S8("cr5"),
    [X86_REGISTER_CR6] = S8("cr6"),
    [X86_REGISTER_CR7] = S8("cr7"),
    [X86_REGISTER_CR8] = S8("cr8"),
    [X86_REGISTER_CR9] = S8("cr9"),
    [X86_REGISTER_CR10] = S8("cr10"),
    [X86_REGISTER_CR11] = S8("cr11"),
    [X86_REGISTER_CR12] = S8("cr12"),
    [X86_REGISTER_CR13] = S8("cr13"),
    [X86_REGISTER_CR14] = S8("cr14"),
    [X86_REGISTER_CR15] = S8("cr15"),
    [X86_REGISTER_DR0] = S8("dr0"),
    [X86_REGISTER_DR1] = S8("dr1"),
    [X86_REGISTER_DR2] = S8("dr2"),
    [X86_REGISTER_DR3] = S8("dr3"),
    [X86_REGISTER_DR4] = S8("dr4"),
    [X86_REGISTER_DR5] = S8("dr5"),
    [X86_REGISTER_DR6] = S8("dr6"),
    [X86_REGISTER_DR7] = S8("dr7"),
};

static_assert(BUSTER_ARRAY_LENGTH(x86_register_name_strings) == X86_REGISTER_COUNT);


// BUSTER_IMPL CodeGeneration module_generation_initialize()
// {
//     return (CodeGeneration){
//         .code_arena = arena_create((ArenaCreation){}),
//         .data_arena = arena_create((ArenaCreation){}),
//     };
// }
//
// BUSTER_GLOBAL_LOCAL void write_prologue(CodeGeneration* generation)
// {
//     u8 push_rbp[] = { 0x55 };
//     u8 mov_rbp_rsp[] = { 0x48, 0x89, 0xe5 };
//     let bytes = arena_allocate(generation->code_arena, u8, BUSTER_ARRAY_LENGTH(push_rbp) + BUSTER_ARRAY_LENGTH(mov_rbp_rsp));
//     memcpy(bytes, push_rbp, sizeof(push_rbp));
//     memcpy(bytes + sizeof(push_rbp), mov_rbp_rsp, sizeof(mov_rbp_rsp));
// }
//
// BUSTER_GLOBAL_LOCAL void write_epilogue(CodeGeneration* generation)
// {
//     u8 pop_rbp[] = { 0x5d };
//     u8 ret[] = { 0xc3 };
//     let bytes = arena_allocate(generation->code_arena, u8, BUSTER_ARRAY_LENGTH(pop_rbp) + BUSTER_ARRAY_LENGTH(ret));
//     memcpy(bytes, pop_rbp, sizeof(pop_rbp));
//     memcpy(bytes + sizeof(pop_rbp), ret, sizeof(ret));
// }
//
// ENUM_T(Register_x86_64, u8,
//     REGISTER_A = 0,
//     REGISTER_C = 1,
//     REGISTER_D = 2,
//     REGISTER_B = 3,
//     REGISTER_SP = 4,
//     REGISTER_BP = 5,
//     REGISTER_SI = 6,
//     REGISTER_DI = 7,
//     REGISTER_08 = 8,
//     REGISTER_09 = 9,
//     REGISTER_10 = 10,
//     REGISTER_11 = 11,
//     REGISTER_12 = 12,
//     REGISTER_13 = 13,
//     REGISTER_14 = 14,
//     REGISTER_15 = 15,
// );
//
// BUSTER_GLOBAL_LOCAL Register_x86_64 system_v_return_registers[] = {
//     REGISTER_A,
//     REGISTER_D,
// };
//
// ENUM_T(MachineInstructionId, u64,
//     MACHINE_INSTRUCTION_X86_RETURN_ZERO_REGISTER,
//     MACHINE_INSTRUCTION_X86_RETURN_IMMEDIATE_REGISTER,
// );
//
// UNION(MachineOperand)
// {
//     u64 constant_integer;
//     f64 constant_float;
//     u8 register_name;
// };
//
// STRUCT(MachineInstruction)
// {
//     MachineOperand operands[4];
//     MachineInstructionId id;
//     u32 selected_form_index;
//     X86SelectorLoweringId lowering_id;
//     u8 reserved[8];
// };
//
// BUSTER_GLOBAL_LOCAL void emit_x86_xor_zero_register(CodeGeneration* generation, u8 register_name)
// {
//     u8 rex = 0x40;
//     rex |= (u8)(((register_name >> 3) & 1) << 2);
//     rex |= (u8)((register_name >> 3) & 1);
//
//     if (rex != 0x40)
//     {
//         let prefix = arena_allocate(generation->code_arena, u8, 1);
//         prefix[0] = rex;
//     }
//
//     u8 bytes[] = {
//         0x31,
//         (u8)(0xc0 | ((register_name & 7) << 3) | (register_name & 7)),
//     };
//     let buffer = arena_allocate(generation->code_arena, u8, BUSTER_ARRAY_LENGTH(bytes));
//     memcpy(buffer, bytes, sizeof(bytes));
// }
//
// BUSTER_GLOBAL_LOCAL void emit_x86_mov_immediate_register(CodeGeneration* generation, u8 register_name, u64 constant_integer, u32 immediate_width_bits)
// {
//     if (immediate_width_bits == 16)
//     {
//         let prefix = arena_allocate(generation->code_arena, u8, 1);
//         prefix[0] = 0x66;
//     }
//
//     u8 rex = 0x40;
//     if (immediate_width_bits == 64)
//     {
//         rex |= 0x08;
//     }
//     rex |= (u8)((register_name >> 3) & 1);
//
//     if (rex != 0x40)
//     {
//         let prefix = arena_allocate(generation->code_arena, u8, 1);
//         prefix[0] = rex;
//     }
//
//     {
//         let opcode = arena_allocate(generation->code_arena, u8, 1);
//         opcode[0] = (u8)(0xb8 | (register_name & 7));
//     }
//
//     if (immediate_width_bits == 8)
//     {
//         let immediate = arena_allocate(generation->code_arena, u8, 1);
//         immediate[0] = (u8)constant_integer;
//     }
//     else if (immediate_width_bits == 16)
//     {
//         let immediate = arena_allocate(generation->code_arena, u16, 1);
//         immediate[0] = (u16)constant_integer;
//     }
//     else if (immediate_width_bits == 32)
//     {
//         let immediate = arena_allocate(generation->code_arena, u32, 1);
//         immediate[0] = (u32)constant_integer;
//     }
//     else
//     {
//         let immediate = arena_allocate(generation->code_arena, u64, 1);
//         immediate[0] = constant_integer;
//     }
// }
//
// BUSTER_IMPL bool function_generate(CodeGeneration* generation, Arena* arena, IrModule* module, IrFunction* function, CodeGenerationOptions options)
// {
//     bool result = true;
//     let original_position = arena->position;
//
//     BUSTER_UNUSED(arena);
//     BUSTER_UNUSED(module);
//     BUSTER_UNUSED(options);
//
//     function->code_position = generation->code_arena->position - sizeof(Arena);
//     write_prologue(generation);
//
//     let block = function->entry_block;
//
//     // Instruction selection
//     let machine_instruction_start = align_forward(arena->position, alignof(MachineInstruction));
//     
//     while (block)
//     {
//         for (IrInstruction* instruction = block->first; instruction; instruction = instruction->next)
//         {
//             switch (instruction->id)
//             {
//                 break; case IR_INSTRUCTION_RETURN:
//                 {
//                     let return_instruction = arena_allocate(arena, MachineInstruction, 1);
//                     let return_value = instruction->value;
//
//                     switch (return_value->id)
//                     {
//                         break; case IR_VALUE_ID_CONSTANT_INTEGER:
//                         {
//                             X86SelectorSelection selection = { 0 };
//                             result = x86_selector_select_return_constant_integer(return_value->type->id, return_value->constant_integer, &selection);
//                             if (result)
//                             {
//                                 return_instruction->selected_form_index = selection.form_index;
//                                 return_instruction->lowering_id = selection.lowering_id;
//                                 return_instruction->operands[0] = (MachineOperand) {
//                                     .register_name = system_v_return_registers[0],
//                                 };
//                                 return_instruction->operands[1] = (MachineOperand) {
//                                     .constant_integer = return_value->constant_integer,
//                                 };
//                                 return_instruction->operands[2] = (MachineOperand) {
//                                     .constant_integer = selection.immediate_width_bits,
//                                 };
//
//                                 if (selection.lowering_id == X86_SELECTOR_LOWERING_XOR_ZERO_GPR32)
//                                 {
//                                     return_instruction->id = MACHINE_INSTRUCTION_X86_RETURN_ZERO_REGISTER;
//                                 }
//                                 else if (selection.lowering_id == X86_SELECTOR_LOWERING_MOV_IMMEDIATE_GPR)
//                                 {
//                                     return_instruction->id = MACHINE_INSTRUCTION_X86_RETURN_IMMEDIATE_REGISTER;
//                                 }
//                                 else
//                                 {
//                                     result = false;
//                                 }
//                             }
//                         }
//                     }
//
//                     block = 0;
//                 }
//             }
//         }
//     }
//
//     let machine_instruction_end = arena->position;
//     let instructions = (MachineInstruction*)((u8*)arena + machine_instruction_start);
//     let byte_range = (machine_instruction_end - machine_instruction_start);
//     BUSTER_CHECK(byte_range % sizeof(MachineInstruction) == 0);
//     let instruction_count = byte_range / sizeof(MachineInstruction);
//
//     // Register allocation
//
//     // Emit
//
//     for (u64 instruction_i = 0; instruction_i < instruction_count; instruction_i += 1)
//     {
//         MachineInstruction* instruction = &instructions[instruction_i];
//
//         switch (instruction->id)
//         {
//             break; case MACHINE_INSTRUCTION_X86_RETURN_ZERO_REGISTER:
//             {
//                 emit_x86_xor_zero_register(generation, instruction->operands[0].register_name);
//             }
//             break; case MACHINE_INSTRUCTION_X86_RETURN_IMMEDIATE_REGISTER:
//             {
//                 emit_x86_mov_immediate_register(
//                     generation,
//                     instruction->operands[0].register_name,
//                     instruction->operands[1].constant_integer,
//                     (u32)instruction->operands[2].constant_integer);
//             }
//         }
//     }
//
//     write_epilogue(generation);
//
//     arena->position = original_position;
//
//     return result;
// }
//


typedef u64 Bitset;

STRUCT(GPR)
{
    Bitset mask[4];
};

STRUCT(VectorOpcode)
{
    Bitset prefix_0f;
    Bitset plus_register;
    u8 values[2][64];
    u8 extension[64];
};

typedef enum LegacyPrefix
{
    LEGACY_PREFIX_F0,
    LEGACY_PREFIX_F2,
    LEGACY_PREFIX_F3,
    LEGACY_PREFIX_2E,
    LEGACY_PREFIX_36,
    LEGACY_PREFIX_3E,
    LEGACY_PREFIX_26,
    LEGACY_PREFIX_64,
    LEGACY_PREFIX_65,
    LEGACY_PREFIX_66,
    LEGACY_PREFIX_67,
    LEGACY_PREFIX_COUNT,
} LegacyPrefix;

BUSTER_GLOBAL_LOCAL u8 legacy_prefixes[] = {
    [LEGACY_PREFIX_F0] = 0xf0,
    [LEGACY_PREFIX_F2] = 0xf2,
    [LEGACY_PREFIX_F3] = 0xf3,
    [LEGACY_PREFIX_2E] = 0x2e,
    [LEGACY_PREFIX_36] = 0x36,
    [LEGACY_PREFIX_3E] = 0x3e,
    [LEGACY_PREFIX_26] = 0x26,
    [LEGACY_PREFIX_64] = 0x64,
    [LEGACY_PREFIX_65] = 0x65,
    [LEGACY_PREFIX_66] = 0x66,
    [LEGACY_PREFIX_67] = 0x67,
};

static_assert(BUSTER_ARRAY_LENGTH(legacy_prefixes) == LEGACY_PREFIX_COUNT);

typedef enum GPR_x86_64
{
    REGISTER_X86_64_AL  = 0x0,
    REGISTER_X86_64_AH  = REGISTER_X86_64_AL | (1 << 2),
    REGISTER_X86_64_AX  = REGISTER_X86_64_AL,
    REGISTER_X86_64_EAX = REGISTER_X86_64_AL,
    REGISTER_X86_64_RAX = REGISTER_X86_64_AL,

    REGISTER_X86_64_CL  = 0x1,
    REGISTER_X86_64_CH  = REGISTER_X86_64_CL | (1 << 2),
    REGISTER_X86_64_CX  = REGISTER_X86_64_CL,
    REGISTER_X86_64_ECX = REGISTER_X86_64_CL,
    REGISTER_X86_64_RCX = REGISTER_X86_64_CL,

    REGISTER_X86_64_DL  = 0x2,
    REGISTER_X86_64_DH  = REGISTER_X86_64_DL | (1 << 2),
    REGISTER_X86_64_DX  = REGISTER_X86_64_DL,
    REGISTER_X86_64_EDX = REGISTER_X86_64_DL,
    REGISTER_X86_64_RDX = REGISTER_X86_64_DL,

    REGISTER_X86_64_BL  = 0x3,
    REGISTER_X86_64_BH  = REGISTER_X86_64_BL | (1 << 2),
    REGISTER_X86_64_BX  = REGISTER_X86_64_BL,
    REGISTER_X86_64_EBX = REGISTER_X86_64_BL,
    REGISTER_X86_64_RBX = REGISTER_X86_64_BL,

    REGISTER_X86_64_SPL = 0x4,
    REGISTER_X86_64_SP  = REGISTER_X86_64_SPL,
    REGISTER_X86_64_ESP = REGISTER_X86_64_SPL,
    REGISTER_X86_64_RSP = REGISTER_X86_64_SPL,

    REGISTER_X86_64_BPL = 0x5,
    REGISTER_X86_64_BP  = REGISTER_X86_64_BPL,
    REGISTER_X86_64_EBP = REGISTER_X86_64_BPL,
    REGISTER_X86_64_RBP = REGISTER_X86_64_BPL,

    REGISTER_X86_64_SIL = 0x6,
    REGISTER_X86_64_SI  = REGISTER_X86_64_SIL,
    REGISTER_X86_64_ESI = REGISTER_X86_64_SIL,
    REGISTER_X86_64_RSI = REGISTER_X86_64_SIL,

    REGISTER_X86_64_DIL = 0x7,
    REGISTER_X86_64_DI  = REGISTER_X86_64_DIL,
    REGISTER_X86_64_EDI = REGISTER_X86_64_DIL,
    REGISTER_X86_64_RDI = REGISTER_X86_64_DIL,

    REGISTER_X86_64_R8L = 0x8,
    REGISTER_X86_64_R8W = REGISTER_X86_64_R8L,
    REGISTER_X86_64_R8D = REGISTER_X86_64_R8L,
    REGISTER_X86_64_R8  = REGISTER_X86_64_R8L,

    REGISTER_X86_64_R9L = 0x9,
    REGISTER_X86_64_R9W = REGISTER_X86_64_R9L,
    REGISTER_X86_64_R9D = REGISTER_X86_64_R9L,
    REGISTER_X86_64_R9  = REGISTER_X86_64_R9L,

    REGISTER_X86_64_R10L = 0xa,
    REGISTER_X86_64_R10W = REGISTER_X86_64_R10L,
    REGISTER_X86_64_R10D = REGISTER_X86_64_R10L,
    REGISTER_X86_64_R10  = REGISTER_X86_64_R10L,

    REGISTER_X86_64_R11L = 0xb,
    REGISTER_X86_64_R11W = REGISTER_X86_64_R11L,
    REGISTER_X86_64_R11D = REGISTER_X86_64_R11L,
    REGISTER_X86_64_R11  = REGISTER_X86_64_R11L,

    REGISTER_X86_64_R12L = 0xc,
    REGISTER_X86_64_R12W = REGISTER_X86_64_R12L,
    REGISTER_X86_64_R12D = REGISTER_X86_64_R12L,
    REGISTER_X86_64_R12  = REGISTER_X86_64_R12L,

    REGISTER_X86_64_R13L = 0xd,
    REGISTER_X86_64_R13W = REGISTER_X86_64_R13L,
    REGISTER_X86_64_R13D = REGISTER_X86_64_R13L,
    REGISTER_X86_64_R13  = REGISTER_X86_64_R13L,

    REGISTER_X86_64_R14L = 0xe,
    REGISTER_X86_64_R14W = REGISTER_X86_64_R14L,
    REGISTER_X86_64_R14D = REGISTER_X86_64_R14L,
    REGISTER_X86_64_R14  = REGISTER_X86_64_R14L,

    REGISTER_X86_64_R15L = 0xf,
    REGISTER_X86_64_R15W = REGISTER_X86_64_R15L,
    REGISTER_X86_64_R15D = REGISTER_X86_64_R15L,
    REGISTER_X86_64_R15  = REGISTER_X86_64_R15L,
} GPR_x86_64;

#define batch_element_count (64)
#define max_instruction_byte_count (16)

STRUCT(EncodingBatch)
{
    Bitset legacy_prefixes[LEGACY_PREFIX_COUNT];
    Bitset is_rm_register;
    Bitset is_reg_register;
    GPR rm_register;
    GPR reg_register;
    Bitset implicit_register;
    VectorOpcode opcode;
    Bitset is_relative;
    Bitset is_displacement;
    Bitset displacement_size;
    Bitset rex_w;
    u8 segment_register_override[64];
    Bitset is_immediate;
    Bitset immediate_size[2];
    u8 immediate[8][64];
    u8 displacement[4][64];
};

BUSTER_GLOBAL_LOCAL u32 encode_wide(u8* restrict buffer, const EncodingBatch* const restrict batch)
{
    __m512i prefixes[LEGACY_PREFIX_COUNT];
    __mmask64 prefix_masks[LEGACY_PREFIX_COUNT];
    for (LegacyPrefix prefix = 0; prefix < LEGACY_PREFIX_COUNT; prefix += 1)
    {
        prefix_masks[prefix] = _cvtu64_mask64(batch->legacy_prefixes[prefix]);
        prefixes[prefix] = _mm512_maskz_set1_epi8(prefix_masks[prefix], legacy_prefixes[prefix]);
    }

    __m512i instruction_length;

    u8 prefix_group1_bytes[64];
    u8 prefix_group1_positions[64];
    {
        __mmask64 prefix_group1_mask = _kor_mask64(_kor_mask64(prefix_masks[LEGACY_PREFIX_F0], prefix_masks[LEGACY_PREFIX_F2]), prefix_masks[LEGACY_PREFIX_F3]);
        __m512i prefix_group1 = _mm512_or_epi32(_mm512_or_epi32(prefixes[LEGACY_PREFIX_F0], prefixes[LEGACY_PREFIX_F2]), prefixes[LEGACY_PREFIX_F3]);
        __m512i prefix_group1_position = _mm512_maskz_set1_epi8(_knot_mask64(prefix_group1_mask), 0x0f);
        instruction_length = _mm512_maskz_set1_epi8(prefix_group1_mask, 0x01);

        _mm512_storeu_epi8(prefix_group1_bytes, prefix_group1);
        _mm512_storeu_epi8(prefix_group1_positions, prefix_group1_position);
    }

    u8 prefix_group2_bytes[64];
    u8 prefix_group2_positions[64];
    {
        __mmask64 prefix_group2_mask = _kor_mask64(_kor_mask64(_kor_mask64(prefix_masks[LEGACY_PREFIX_2E], prefix_masks[LEGACY_PREFIX_36]), _kor_mask64(prefix_masks[LEGACY_PREFIX_3E], prefix_masks[LEGACY_PREFIX_26])), _kor_mask64(prefix_masks[LEGACY_PREFIX_64], prefix_masks[LEGACY_PREFIX_65]));
        __m512i prefix_group2 = _mm512_or_epi32(_mm512_or_epi32(_mm512_or_epi32(prefixes[LEGACY_PREFIX_2E], prefixes[LEGACY_PREFIX_36]), _mm512_or_epi32(prefixes[LEGACY_PREFIX_3E], prefixes[LEGACY_PREFIX_26])), _mm512_or_epi32(prefixes[LEGACY_PREFIX_64], prefixes[LEGACY_PREFIX_65]));
        __m512i prefix_group2_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), prefix_group2_mask, instruction_length);
        instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(prefix_group2_mask, 0x01));

        _mm512_storeu_epi8(prefix_group2_bytes, prefix_group2);
        _mm512_storeu_epi8(prefix_group2_positions, prefix_group2_position);
    }

    u8 prefix_group3_bytes[64];
    u8 prefix_group3_positions[64];
    {
        __mmask64 prefix_group3_mask = prefix_masks[LEGACY_PREFIX_66];
        __m512i prefix_group3 = prefixes[LEGACY_PREFIX_66];
        __m512i prefix_group3_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), prefix_group3_mask, instruction_length);
        instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(prefix_group3_mask, 0x01));

        _mm512_storeu_epi8(prefix_group3_bytes, prefix_group3);
        _mm512_storeu_epi8(prefix_group3_positions, prefix_group3_position);
    }

    u8 prefix_group4_bytes[64];
    u8 prefix_group4_positions[64];
    {
        __mmask64 prefix_group4_mask = prefix_masks[LEGACY_PREFIX_67];
        __m512i prefix_group4 = prefixes[LEGACY_PREFIX_67];
        __m512i prefix_group4_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), prefix_group4_mask, instruction_length);
        instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(prefix_group4_mask, 0x01));

        _mm512_storeu_epi8(prefix_group4_bytes, prefix_group4);
        _mm512_storeu_epi8(prefix_group4_positions, prefix_group4_position);
    }

    __mmask64 is_plus_register = _cvtu64_mask64(batch->opcode.plus_register);
    __mmask64 is_implicit_register = _cvtu64_mask64(batch->implicit_register);

    __mmask64 is_displacement8 = _kand_mask64(_cvtu64_mask64(batch->is_displacement), _knot_mask64(_cvtu64_mask64(batch->displacement_size)));
    __mmask64 is_displacement32 = _kand_mask64(_cvtu64_mask64(batch->is_displacement), _cvtu64_mask64(batch->displacement_size));

    __mmask64 is_relative8 = _kand_mask64(_cvtu64_mask64(batch->is_relative), _knot_mask64(_cvtu64_mask64(batch->displacement_size)));
    __mmask64 is_relative32 = _kand_mask64(_cvtu64_mask64(batch->is_relative), _cvtu64_mask64(batch->displacement_size));

    __mmask64 is_rm_register;
    __m512i rm_register;
    {
        __m256i register_mask_256 = _mm256_loadu_epi8(&batch->rm_register);
        __m256i selecting_mask = _mm256_set1_epi8(0x0f);
        __m256i low_bits = _mm256_and_si256(register_mask_256, selecting_mask);
        __m256i high_bits = _mm256_and_si256(_mm256_srli_epi64(register_mask_256, 4), selecting_mask);
        __m256i low_bytes = _mm256_unpacklo_epi8(low_bits, high_bits);
        __m256i high_bytes = _mm256_unpackhi_epi8(low_bits, high_bits);
        rm_register = _mm512_inserti64x4(_mm512_castsi256_si512(low_bytes), high_bytes, 1);
        is_rm_register = _cvtu64_mask64(batch->is_rm_register);
    }

    __mmask64 is_reg_register;
    __m512i reg_register;
    {
        __m256i register_mask_256 = _mm256_loadu_epi8(&batch->reg_register);
        __m256i selecting_mask = _mm256_set1_epi8(0x0f);
        __m256i low_bits = _mm256_and_si256(register_mask_256, selecting_mask);
        __m256i high_bits = _mm256_and_si256(_mm256_srli_epi64(register_mask_256, 4), selecting_mask);
        __m256i low_bytes = _mm256_unpacklo_epi8(low_bits, high_bits);
        __m256i high_bytes = _mm256_unpackhi_epi8(low_bits, high_bits);
        reg_register = _mm512_inserti64x4(_mm512_castsi256_si512(low_bytes), high_bytes, 1);
        is_reg_register = _cvtu64_mask64(batch->is_reg_register);
    }

    __mmask64 is_reg_direct_addressing_mode = _knot_mask64(_kor_mask64(is_displacement8, is_displacement32));
    __mmask64 has_base_register = _kor_mask64(_kor_mask64(is_rm_register, is_reg_register), is_implicit_register);

    __m512i rex_b = _mm512_maskz_set1_epi8(_mm512_test_epi8_mask(rm_register, _mm512_set1_epi8(0b1000)), 1 << 0);
    __m512i rex_x = _mm512_set1_epi8(0); // TODO
    __m512i rex_r = _mm512_maskz_set1_epi8(_mm512_test_epi8_mask(reg_register, _mm512_set1_epi8(0b1000)), 1 << 2);
    __m512i rex_w = _mm512_maskz_set1_epi8(_cvtu64_mask64(batch->rex_w), 1 << 3);
    __m512i rex_byte = _mm512_or_epi32(_mm512_set1_epi32(0x40), _mm512_or_epi32(_mm512_or_epi32(rex_b, rex_x), _mm512_or_epi32(rex_r, rex_w)));
    __mmask64 rex_mask = _mm512_test_epi8_mask(rex_byte, _mm512_set1_epi8(0x0f));
    __m512i rex_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), rex_mask, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(rex_mask, 0x01));

    u8 rex_bytes[64];
    u8 rex_positions[64];
    _mm512_storeu_epi8(rex_bytes, rex_byte);
    _mm512_storeu_epi8(rex_positions, rex_position);

    __m512i plus_register = _mm512_and_si512(rm_register, _mm512_set1_epi8(0b111));
    __m512i opcode_extension = _mm512_loadu_epi8(&batch->opcode.extension[0]);

    __mmask64 prefix_0f_mask = _cvtu64_mask64(batch->opcode.prefix_0f);
    __m512i prefix_0f = _mm512_maskz_set1_epi8(prefix_0f_mask, 0x0f);
    __m512i prefix_0f_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), prefix_0f_mask, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(prefix_0f_mask, 0x01));

    u8 prefix_0f_bytes[64];
    u8 prefix_0f_positions[64];
    _mm512_storeu_epi8(prefix_0f_bytes, prefix_0f);
    _mm512_storeu_epi8(prefix_0f_positions, prefix_0f_position);

    __m512i three_byte_opcode = _mm512_loadu_epi8(&batch->opcode.values[1]);
    __mmask64 three_byte_opcode_mask = _mm512_test_epi8_mask(three_byte_opcode, _mm512_set1_epi8(0xff));
    __m512i three_byte_opcode_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), three_byte_opcode_mask, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(three_byte_opcode_mask, 0x01));

    u8 three_byte_opcode_bytes[64];
    u8 three_byte_opcode_positions[64];
    _mm512_storeu_epi8(three_byte_opcode_bytes, three_byte_opcode);
    _mm512_storeu_epi8(three_byte_opcode_positions, three_byte_opcode_position);
    
    __m512i base_opcode = _mm512_or_epi32(_mm512_loadu_epi8(&batch->opcode.values[0]), _mm512_maskz_mov_epi8(is_plus_register, plus_register));
    __m512i base_opcode_position = instruction_length;
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_set1_epi8(0x01));

    u8 base_opcode_bytes[64];
    u8 base_opcode_positions[64];
    _mm512_storeu_epi8(base_opcode_bytes, base_opcode);
    _mm512_storeu_epi8(base_opcode_positions, base_opcode_position);

    __m512i displacement8 = _mm512_loadu_epi8(batch->displacement[0]);
    __mmask64 mod_is_displacement32 = is_displacement32;
    __mmask64 mod_is_displacement8 = _kand_mask64(is_displacement8, _kor_mask64(_mm512_test_epi8_mask(displacement8, displacement8), _kand_mask64(is_rm_register, _mm512_cmpeq_epi8_mask(_mm512_and_si512(rm_register, _mm512_set1_epi8(0b111)), _mm512_set1_epi8(REGISTER_X86_64_BP)))));
    
    __mmask64 mod_rm_mask = _kor_mask64(_kand_mask64(_kor_mask64(is_rm_register, is_reg_register), _knot_mask64(is_plus_register)), _kor_mask64(is_displacement8, is_displacement32));
    __m512i register_direct_address_mode = _mm512_maskz_set1_epi8(is_reg_direct_addressing_mode, 1);
    __m512i mod = _mm512_or_epi32(_mm512_or_epi32(_mm512_slli_epi32(_mm512_maskz_set1_epi8(_kand_mask64(mod_is_displacement32, has_base_register), 1), 1), _mm512_maskz_set1_epi8(mod_is_displacement8, 1)), _mm512_or_epi32(_mm512_slli_epi32(register_direct_address_mode, 1), register_direct_address_mode));
    __m512i rm = _mm512_or_epi32(_mm512_and_si512(rm_register, _mm512_set1_epi8(0b111)), _mm512_maskz_set1_epi8(_knot_mask64(has_base_register), 0b100));
    __m512i reg = _mm512_or_epi32(_mm512_and_si512(reg_register, _mm512_set1_epi8(0b111)), opcode_extension);
    __m512i mod_rm = _mm512_or_epi32(_mm512_or_epi32(rm, _mm512_slli_epi32(reg, 3)), _mm512_slli_epi32(mod, 6));
    __m512i mod_rm_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), mod_rm_mask, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(mod_rm_mask, 0x01));

    u8 mod_rm_bytes[64];
    u8 mod_rm_positions[64];
    _mm512_storeu_epi8(mod_rm_bytes, mod_rm);
    _mm512_storeu_epi8(mod_rm_positions, mod_rm_position);

    __mmask64 sib_mask = _kand_mask64(_mm512_cmpneq_epi8_mask(mod, _mm512_set1_epi8(0b11)), _mm512_cmpeq_epi8_mask(rm, _mm512_set1_epi8(0b100)));
    __m512i sib_scale = _mm512_set1_epi8(0);
    __m512i sib_index = _mm512_maskz_set1_epi8(sib_mask, 0b100 << 3);
    __m512i sib_base = _mm512_or_epi32(_mm512_and_si512(rm_register, _mm512_maskz_set1_epi8(is_rm_register, 0b111)), _mm512_maskz_set1_epi8(_knot_mask64(is_rm_register), 0b101));
    __m512i sib = _mm512_or_epi32(_mm512_or_epi32(sib_index, sib_base), sib_scale);
    __m512i sib_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), sib_mask, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(sib_mask, 0x01));

    u8 sib_bytes[64];
    u8 sib_positions[64];
    _mm512_storeu_epi8(sib_bytes, sib);
    _mm512_storeu_epi8(sib_positions, sib_position);

    __m512i displacement8_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), mod_is_displacement8, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(mod_is_displacement8, sizeof(s8)));
    u8 displacement8_positions[64];
    _mm512_storeu_epi8(displacement8_positions, displacement8_position);

    __m512i displacement32_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), mod_is_displacement32, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(mod_is_displacement32, sizeof(s32)));
    u8 displacement32_positions[64];
    _mm512_storeu_epi8(displacement32_positions, displacement32_position);

    __m512i relative8_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), is_relative8, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(is_relative8, sizeof(s8)));
    u8 relative8_positions[64];
    _mm512_storeu_epi8(relative8_positions, relative8_position);

    __m512i relative32_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), is_relative32, instruction_length);
    instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(is_relative32, sizeof(s32)));
    u8 relative32_positions[64];
    _mm512_storeu_epi8(relative32_positions, relative32_position);

    __mmask64 is_immediate_mask = _cvtu64_mask64(batch->is_immediate);
    __mmask64 mask0 = _cvtu64_mask64(batch->immediate_size[0]);
    __m512i mask_v0 = _mm512_maskz_set1_epi8(_kand_mask64(is_immediate_mask, mask0), 1 << 0);
    __mmask64 mask1 = _cvtu64_mask64(batch->immediate_size[1]);
    __m512i mask_v1 = _mm512_maskz_set1_epi8(_kand_mask64(is_immediate_mask, mask1), 1 << 1);
    __m512i immediate_size = _mm512_or_si512(mask_v0, mask_v1);
    __mmask64 is_immediate[4];
    u8 immediate_positions[BUSTER_ARRAY_LENGTH(is_immediate)][64];
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(is_immediate); i += 1)
    {
        __mmask64 immediate_mask = _mm512_cmpeq_epi8_mask(immediate_size, _mm512_set1_epi8((char8)i));
        __m512i immediate_position = _mm512_mask_mov_epi8(_mm512_set1_epi8(0x0f), immediate_mask, instruction_length);
        instruction_length = _mm512_add_epi8(instruction_length, _mm512_maskz_set1_epi8(immediate_mask, (u8)(1 << i)));
        _mm512_storeu_epi8(immediate_positions[i], immediate_position);
    }

    u8 separate_buffers[64][max_instruction_byte_count];
    u8 separate_lengths[64];
    _mm512_storeu_epi8(separate_lengths, instruction_length);

    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(separate_lengths); i += 1)
    {
        separate_buffers[i][prefix_group1_positions[i]] = prefix_group1_bytes[i];
        separate_buffers[i][prefix_group2_positions[i]] = prefix_group2_bytes[i];
        separate_buffers[i][prefix_group3_positions[i]] = prefix_group3_bytes[i];
        separate_buffers[i][prefix_group4_positions[i]] = prefix_group4_bytes[i];

        separate_buffers[i][rex_positions[i]] = rex_bytes[i];

        separate_buffers[i][prefix_0f_positions[i]] = prefix_0f_bytes[i];
        separate_buffers[i][three_byte_opcode_positions[i]] = three_byte_opcode_bytes[i];
        separate_buffers[i][base_opcode_positions[i]] = base_opcode_bytes[i];

        separate_buffers[i][mod_rm_positions[i]] = mod_rm_bytes[i];

        separate_buffers[i][sib_positions[i]] = sib_bytes[i];

        for (u32 immediate_position_index = 0; immediate_position_index < BUSTER_ARRAY_LENGTH(immediate_positions); immediate_position_index += 1)
        {
            u8 start_position = immediate_positions[immediate_position_index][i];
            for (u32 byte = 0; byte < 1 << immediate_position_index; byte += 1)
            {
                let destination_index = (u8)(start_position + byte * (start_position != 0xf));
                separate_buffers[i][destination_index] = batch->immediate[byte][i];
            }
        }

        separate_buffers[i][displacement8_positions[i]] = batch->displacement[0][i];

        u8 displacement32_start = displacement32_positions[i];
        for (u32 byte = 0; byte < 4; byte += 1)
        {
            let destination_index = displacement32_start + byte * (displacement32_start != 0xf);
            separate_buffers[i][destination_index] = batch->displacement[byte][i];
        }

        separate_buffers[i][relative8_positions[i]] = batch->displacement[0][i];
        
        u8 relative32_start = relative32_positions[i];
        for (u32 byte = 0; byte < 4; byte += 1)
        {
            let destination_index = relative32_start + byte * (relative32_start != 0xf);
            separate_buffers[i][destination_index] = batch->displacement[byte][i];
        }
    }

    u32 buffer_i = 0;

    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(separate_lengths); i += 1)
    {
        let separate_length = separate_lengths[i];
        if (separate_length >= 1 && separate_length <= 15)
        {
            memcpy(&buffer[buffer_i], &separate_buffers[i], separate_length);
            buffer_i += separate_length;
        }
        else
        {
            BUSTER_UNREACHABLE();
        }
    }

    return buffer_i;
}

BUSTER_IMPL void parse_assembly(Arena* arena)
{
    u32 alignment = 4 * 512 / 8;
    u32 start_padding = 0;
    u32 end_padding = 64; // Padding for safe SIMD reads past content
    let file = file_read(arena, SOs("tests/assembly.S"), (FileReadOptions){ .start_padding = start_padding, .start_alignment = alignment, .end_padding = end_padding, .end_alignment = alignment });

    let bottom = (char8*)file.pointer;
    let top = bottom + file.length;
    let pointer = bottom;
    u64 total_line_count = 0;

    constexpr u64 bit_count = 2048;
    constexpr u64 byte_count = bit_count / 8;
    constexpr u64 lane_bit_count = 512;
    constexpr u64 lane_byte_count = lane_bit_count / 8;
    constexpr u64 lane_count = bit_count / lane_bit_count;

    let line_feed_lane = _mm512_set1_epi8('\n');

    __m512i indices[lane_count];
    __m512i index_base = _mm512_set_epi8(
                63, 62, 61, 60, 59, 58, 57, 56,
                55, 54, 53, 52, 51, 50, 49, 48,
                47, 46, 45, 44, 43, 42, 41, 40,
                39, 38, 37, 36, 35, 34, 33, 32,
                31, 30, 29, 28, 27, 26, 25, 24,
                23, 22, 21, 20, 19, 18, 17, 16,
                15, 14, 13, 12, 11, 10, 9, 8,
                7, 6, 5, 4, 3, 2, 1, 0
                );

    for (u64 i = 0; i < lane_count; i += 1)
    {
        indices[i] = _mm512_add_epi8(index_base, _mm512_set1_epi8((u8)i * 64));
    }

    while (top - pointer > 0)
    {
        __m512i chunks[lane_count];

        for (u64 i = 0; i < lane_count; i += 1)
        {
            chunks[i] = _mm512_loadu_epi64(pointer + i * lane_byte_count);
        }

        u64 iteration_line_count = 0;

        __m512i compress[lane_count];

        for (u64 i = 0; i < lane_count; i += 1)
        {
            let is_line_feed = _mm512_cmpeq_epi8_mask(chunks[i], line_feed_lane);
            compress[i] = _mm512_maskz_compress_epi8(is_line_feed, indices[i]);
            iteration_line_count += (u64)__builtin_popcountll(_cvtmask64_u64(is_line_feed));
        }
        
        total_line_count += iteration_line_count;
        pointer += byte_count;
    }

    string8_print(S8("Total line count: {u32}\n"), total_line_count);
}
