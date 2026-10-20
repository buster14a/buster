// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Generated functions for branch sizing's tests and benchmark, until the code generator's (#136) exist: records shaped
// like compiler output, with branches among them as the code generator will emit them (each in its rel8 form with
// displacement 0, and the branch columns beside the records), and chains of branches that each need the next's growth
// to go out of range, the rare cases' shape.
//
// Entry points: branch_sizing_generate (a random function of a BranchSizingShape), branch_sizing_chain (a chain) and
// branch_sizing_filler_of (filler of an exact length). branch_sizing_test.c and branch_sizing_benchmark.c include it
// after x86_64_encoder.c; every function in it is static.

#include <stdint.h>
#include <stdbool.h>

// The operands of a filler pattern.
typedef enum BranchSizingOperands
{
    // Register operands (or none).
    BRANCH_SIZING_REGISTERS,
    // [rsp + disp8] or [rbp + disp8]: a stack slot.
    BRANCH_SIZING_STACK,
    // [base + disp], with an 8- or 32-bit displacement.
    BRANCH_SIZING_BASE,
    // [base + index * scale + disp]
    BRANCH_SIZING_BASE_INDEX,
    // [rip + disp32] to a global: marked BUSTER_X86_FORM_FIXUP, with a placeholder of 0.
    BRANCH_SIZING_RIP,
    // A call to another function: CALL_REL32 marked BUSTER_X86_FORM_FIXUP.
    BRANCH_SIZING_CALL,
} BranchSizingOperands;

typedef struct BranchSizingPattern BranchSizingPattern;
struct BranchSizingPattern
{
    uint16_t form;
    uint8_t operands;
    uint16_t weight;
};

// The filler between the branches: a mix of forms and operands shaped like compiler output, as the encoder benchmark's
// is, without branches, and with a record of no bytes now and then. The weights are per mille. The immediates are
// ones GNU as encodes with the same forms (see branch_sizing_filler), so that the tests can compare with its
// relaxation.
static const BranchSizingPattern branch_sizing_patterns[] = {
    { X86_FORM_MOV_RM64_R64, BRANCH_SIZING_REGISTERS, 100 },
    { X86_FORM_MOV_RM32_R32, BRANCH_SIZING_REGISTERS, 30 },
    { X86_FORM_MOV_R64_RM64, BRANCH_SIZING_STACK, 80 },
    { X86_FORM_MOV_RM64_R64, BRANCH_SIZING_STACK, 60 },
    { X86_FORM_MOV_R32_RM32, BRANCH_SIZING_BASE, 30 },
    { X86_FORM_MOV_R32_RM32, BRANCH_SIZING_BASE_INDEX, 20 },
    { X86_FORM_MOV_RM32_R32, BRANCH_SIZING_BASE, 20 },
    { X86_FORM_MOV_R32_IMM32, BRANCH_SIZING_REGISTERS, 40 },
    { X86_FORM_MOV_R64_IMM64, BRANCH_SIZING_REGISTERS, 5 },
    { X86_FORM_MOV_RM32_IMM32, BRANCH_SIZING_STACK, 10 },
    { X86_FORM_LEA_R64_M, BRANCH_SIZING_BASE, 30 },
    { X86_FORM_LEA_R64_M, BRANCH_SIZING_BASE_INDEX, 10 },
    { X86_FORM_LEA_R64_M, BRANCH_SIZING_RIP, 20 },
    { X86_FORM_ADD_RM64_IMM8, BRANCH_SIZING_REGISTERS, 30 },
    { X86_FORM_SUB_RM64_IMM8, BRANCH_SIZING_REGISTERS, 30 },
    { X86_FORM_ADD_RM64_R64, BRANCH_SIZING_REGISTERS, 20 },
    { X86_FORM_SUB_RM32_R32, BRANCH_SIZING_REGISTERS, 10 },
    { X86_FORM_XOR_RM32_R32, BRANCH_SIZING_REGISTERS, 15 },
    { X86_FORM_ADD_RM32_IMM32, BRANCH_SIZING_REGISTERS, 10 },
    { X86_FORM_CMP_RM64_R64, BRANCH_SIZING_REGISTERS, 20 },
    { X86_FORM_CMP_RM32_IMM8, BRANCH_SIZING_REGISTERS, 25 },
    { X86_FORM_CMP_RM64_IMM8, BRANCH_SIZING_STACK, 5 },
    { X86_FORM_TEST_RM64_R64, BRANCH_SIZING_REGISTERS, 25 },
    { X86_FORM_TEST_RM8_R8, BRANCH_SIZING_REGISTERS, 10 },
    { X86_FORM_CALL_REL32, BRANCH_SIZING_CALL, 40 },
    { X86_FORM_PUSH_R64, BRANCH_SIZING_REGISTERS, 20 },
    { X86_FORM_POP_R64, BRANCH_SIZING_REGISTERS, 20 },
    { X86_FORM_MOVZX_R32_RM8, BRANCH_SIZING_REGISTERS, 10 },
    { X86_FORM_MOVZX_R32_RM8, BRANCH_SIZING_BASE, 10 },
    { X86_FORM_MOVSXD_R64_RM32, BRANCH_SIZING_REGISTERS, 10 },
    { X86_FORM_IMUL_R64_RM64, BRANCH_SIZING_REGISTERS, 10 },
    { X86_FORM_SHL_RM64_IMM8, BRANCH_SIZING_REGISTERS, 8 },
    { X86_FORM_SHR_RM32_IMM8, BRANCH_SIZING_REGISTERS, 6 },
    { X86_FORM_SETE_RM8, BRANCH_SIZING_REGISTERS, 5 },
    { X86_FORM_CMOVE_R64_RM64, BRANCH_SIZING_REGISTERS, 5 },
    { X86_FORM_MOVSD_X_XM64, BRANCH_SIZING_STACK, 10 },
    { X86_FORM_MOVSD_XM64_X, BRANCH_SIZING_STACK, 8 },
    { X86_FORM_ADDSD_X_XM64, BRANCH_SIZING_REGISTERS, 8 },
    { X86_FORM_MOVUPS_X_XM, BRANCH_SIZING_BASE, 3 },
    { X86_FORM_VPADDD_Z_Z_ZM512, BRANCH_SIZING_REGISTERS, 5 },
    { X86_FORM_VMOVDQU64_Z_ZM512, BRANCH_SIZING_BASE_INDEX, 5 },
    { X86_FORM_RET, BRANCH_SIZING_REGISTERS, 15 },
    { X86_FORM_NOP, BRANCH_SIZING_REGISTERS, 2 },
    { X86_FORM_NONE, BRANCH_SIZING_REGISTERS, 5 },
};

// splitmix64.
static uint64_t branch_sizing_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t x = *state;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static uint32_t branch_sizing_below(uint64_t* state, uint32_t bound)
{
    return (uint32_t)((branch_sizing_random(state) >> 32) * bound >> 32);
}

// A register of the operand's class: 0 to 15, or for an EVEX form's vector register, 0 to 31.
static uint8_t branch_sizing_register(uint64_t* state, uint16_t operand, uint8_t family)
{
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    const bool wide = family == BUSTER_X86_FAMILY_EVEX && class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM;
    return (uint8_t)branch_sizing_below(state, wide ? 32 : 16);
}

// A filler record of the pattern. Immediates that GNU as would encode with another form are avoided: an imm8 shift
// by 1 (D1's), and an imm32 that fits in a sign-extended byte (83's).
static X86Instruction branch_sizing_filler(uint64_t* state, const BranchSizingPattern* pattern)
{
    const X86FormDescription* form = &x86_form_descriptions[pattern->form];
    X86Instruction result;
    bool done = false;
    while (!done)
    {
        result = (X86Instruction){ .form = pattern->form };
        if (form->encoding == BUSTER_X86_ENCODING_R)
        {
            result.reg = branch_sizing_register(state, form->reg, form->family);
        }
        if (form->vvvv != BUSTER_X86_OPERAND_NONE)
        {
            result.vvvv = branch_sizing_register(state, form->vvvv, form->family);
        }
        switch (pattern->operands)
        {
            case BRANCH_SIZING_REGISTERS:
                if (form->rm != BUSTER_X86_OPERAND_NONE)
                {
                    result.rm = branch_sizing_register(state, form->rm, form->family);
                }
                break;
            case BRANCH_SIZING_STACK:
                result.mode = BUSTER_X86_MEMORY_BASE;
                result.rm = branch_sizing_below(state, 2) == 0 ? X86_REGISTER_RSP : X86_REGISTER_RBP;
                result.displacement = 8 * ((int32_t)branch_sizing_below(state, 32) - 16);
                break;
            case BRANCH_SIZING_BASE:
                result.mode = BUSTER_X86_MEMORY_BASE;
                result.rm = (uint8_t)branch_sizing_below(state, 16);
                result.displacement = branch_sizing_below(state, 2) == 0 ? (int32_t)branch_sizing_below(state, 256) - 128 : (int32_t)branch_sizing_below(state, 1u << 20);
                break;
            case BRANCH_SIZING_BASE_INDEX:
                result.mode = BUSTER_X86_MEMORY_BASE_INDEX(branch_sizing_below(state, 4));
                result.rm = (uint8_t)branch_sizing_below(state, 16);
                result.index = (uint8_t)branch_sizing_below(state, 15);
                result.index += result.index >= X86_REGISTER_RSP;
                result.displacement = branch_sizing_below(state, 2) == 0 ? (int32_t)branch_sizing_below(state, 256) - 128 : (int32_t)branch_sizing_below(state, 1u << 20);
                break;
            case BRANCH_SIZING_RIP:
                result.mode = BUSTER_X86_MEMORY_RIP;
                result.form |= BUSTER_X86_FORM_FIXUP;
                break;
            default:
                result.form |= BUSTER_X86_FORM_FIXUP;
                break;
        }
        if (form->immediate_bytes == 1)
        {
            result.immediate = 2 + (int32_t)branch_sizing_below(state, 30);
        }
        else if (form->immediate_bytes == 4)
        {
            result.immediate = 128 + (int32_t)branch_sizing_below(state, 1u << 30);
        }
        else if (form->immediate_bytes == 8)
        {
            result.immediate = (int32_t)(uint32_t)branch_sizing_random(state);
            result.displacement = (int32_t)(uint32_t)branch_sizing_random(state);
        }
        done = x86_instruction_is_valid(&result);
    }
    return result;
}

// A filler record from the patterns, by weight.
static X86Instruction branch_sizing_random_filler(uint64_t* state)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(branch_sizing_patterns); i += 1)
    {
        total += branch_sizing_patterns[i].weight;
    }
    uint32_t pick = branch_sizing_below(state, total);
    uint32_t i = 0;
    while (pick >= branch_sizing_patterns[i].weight)
    {
        pick -= branch_sizing_patterns[i].weight;
        i += 1;
    }
    return branch_sizing_filler(state, &branch_sizing_patterns[i]);
}

// A branch in its rel8 form, as the code generator emits it: JMP_REL8 or one of the 16 Jcc_REL8, with displacement 0.
static X86Instruction branch_sizing_branch(uint64_t* state, bool jump)
{
    const uint32_t condition = branch_sizing_below(state, 16);
    const uint16_t form = jump ? X86_FORM_JMP_REL8 : (uint16_t)(X86_FORM_JO_REL8 + condition * (X86_FORM_JNO_REL8 - X86_FORM_JO_REL8));
    return (X86Instruction){ .form = form };
}

// The shape of a generated function: `count` records, one in `branch_period` of them a branch on average, a jmp
// `jmp_per_mille` times in a thousand (a jcc otherwise). Its target is far `far_per_mille` times in a thousand, and
// otherwise near:
//   - uniform (`compiler_like` false): a far target is anywhere in the function, and a near one within `near` records
//     of the branch, either way, every distance as likely;
//   - compiler-like: a near target is up to `near` records away and a far one up to the function's size, backward
//     `backward_per_mille` times in a thousand (forward to the record after the branch at the least, and backward to the
//     branch itself), at a distance whose logarithm is about uniform (branch_sizing_log_distance), as compiled code's
//     are: most branches go a few instructions, and a few go across the function (docs/branch-sizing.md has the
//     statistics of clang's code that the benchmark's shapes follow).
typedef struct BranchSizingShape BranchSizingShape;
struct BranchSizingShape
{
    uint64_t count;
    uint32_t branch_period;
    uint32_t near;
    uint32_t far_per_mille;
    uint32_t jmp_per_mille;
    uint32_t backward_per_mille;
    bool compiler_like;
};

// A distance from 1 to `range` (at least 1) whose logarithm is about uniform: an octave [2^b, 2^(b + 1)) of a uniform b
// below the bit length of `range`, and a uniform distance in it, up to `range`.
static uint32_t branch_sizing_log_distance(uint64_t* state, uint32_t range)
{
    const uint32_t octave = branch_sizing_below(state, 32 - (uint32_t)__builtin_clz(range));
    const uint32_t low = 1u << octave;
    const uint32_t width = range - low + 1 < low ? range - low + 1 : low;
    return low + branch_sizing_below(state, width);
}

// A branch's target in a function of the shape, for the branch at record `i`.
static uint32_t branch_sizing_target(uint64_t* state, const BranchSizingShape* shape, uint64_t i)
{
    const bool far = branch_sizing_below(state, 1000) < shape->far_per_mille;
    int64_t target;
    if (!shape->compiler_like && far)
    {
        target = branch_sizing_below(state, (uint32_t)shape->count + 1);
    }
    else if (!shape->compiler_like)
    {
        target = (int64_t)i + (int64_t)branch_sizing_below(state, 2 * shape->near + 1) - shape->near;
    }
    else
    {
        const uint32_t range = far ? (uint32_t)shape->count : shape->near;
        const int64_t distance = branch_sizing_log_distance(state, range != 0 ? range : 1);
        target = branch_sizing_below(state, 1000) < shape->backward_per_mille ? (int64_t)i + 1 - distance : (int64_t)i + distance;
    }
    return (uint32_t)(target < 0 ? 0 : target > (int64_t)shape->count ? (int64_t)shape->count : target);
}

// Writes a function of the shape into `records`, and its branch columns into `branches` and `targets` (room for `count`
// each). Returns the number of branches.
static uint64_t branch_sizing_generate(uint64_t* state, const BranchSizingShape* shape, X86Instruction* records, uint32_t* branches, uint32_t* targets)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < shape->count; i += 1)
    {
        if (branch_sizing_below(state, shape->branch_period) == 0)
        {
            records[i] = branch_sizing_branch(state, branch_sizing_below(state, 1000) < shape->jmp_per_mille);
            branches[result] = (uint32_t)i;
            targets[result] = branch_sizing_target(state, shape, i);
            result += 1;
        }
        else
        {
            records[i] = branch_sizing_random_filler(state);
        }
    }
    return result;
}

// Filler records of exactly `bytes` bytes: MOV r32, imm32 (B8+r, 5 bytes) and NOP (1 byte). Writes them from
// `records` and returns how many.
static uint64_t branch_sizing_filler_of(uint64_t bytes, X86Instruction* records)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < bytes / 5; i += 1)
    {
        records[result] = (X86Instruction){
            .form = X86_FORM_MOV_R32_IMM32,
            .rm = (uint8_t)(i % 8),
            .immediate = 0x12345678,
        };
        result += 1;
    }
    for (uint64_t i = 0; i < bytes % 5; i += 1)
    {
        records[result] = (X86Instruction){ .form = X86_FORM_NOP };
        result += 1;
    }
    return result;
}

// The bytes between the links of a chain: each link's span holds two of them and the next link, 2 * 62 + 2 = 126
// bytes forward and, with itself, 128 backward: in range while the next link is short, and out of it when it grows by
// 3 or 4. The ends' filler puts the chain's last link out of range from the start.
#define BUSTER_BRANCH_SIZING_CHAIN_GAP 62
#define BUSTER_BRANCH_SIZING_CHAIN_END 200

// The number of records branch_sizing_chain writes for `links` links.
#define BUSTER_BRANCH_SIZING_CHAIN_RECORDS(links) ((uint64_t)(links) * (BUSTER_BRANCH_SIZING_CHAIN_GAP / 5 + BUSTER_BRANCH_SIZING_CHAIN_GAP % 5 + 1) + BUSTER_BRANCH_SIZING_CHAIN_END / 5 + 1)

// A chain of `links` branches, each in range until the next one grows, the last one out of range from the start: every
// round grows one link, so it takes as many rounds as links (the rare cases' shape). Forward, link i jumps over link
// i + 1 to the record after the gap that follows it, and the last link over the end's filler; backward, link i jumps
// back over link i - 1 to the gap before it, and the first link back over the start's filler. Writes the records and
// the branch columns (`links` of each), and returns the number of records.
static uint64_t branch_sizing_chain(uint64_t* state, uint64_t links, bool backward, X86Instruction* records, uint32_t* branches, uint32_t* targets)
{
    uint64_t count = 0;
    if (backward)
    {
        count += branch_sizing_filler_of(BUSTER_BRANCH_SIZING_CHAIN_END, records);
        uint64_t previous_gap = 0;
        uint64_t gap = 0;
        for (uint64_t i = 0; i < links; i += 1)
        {
            previous_gap = gap;
            gap = count;
            count += branch_sizing_filler_of(BUSTER_BRANCH_SIZING_CHAIN_GAP, records + count);
            branches[i] = (uint32_t)count;
            targets[i] = (uint32_t)(i == 0 ? 0 : previous_gap);
            records[count] = branch_sizing_branch(state, branch_sizing_below(state, 2) == 0);
            count += 1;
        }
    }
    else
    {
        for (uint64_t i = 0; i < links; i += 1)
        {
            branches[i] = (uint32_t)count;
            records[count] = branch_sizing_branch(state, branch_sizing_below(state, 2) == 0);
            count += 1;
            count += branch_sizing_filler_of(BUSTER_BRANCH_SIZING_CHAIN_GAP, records + count);
        }
        // Link i's target is the record after link i + 1's gap: link i + 2's, or for the last two links, the end's
        // filler and the function's end.
        for (uint64_t i = 0; i < links; i += 1)
        {
            targets[i] = i + 2 < links ? branches[i + 2] : (uint32_t)count;
        }
        count += branch_sizing_filler_of(BUSTER_BRANCH_SIZING_CHAIN_END, records + count);
        if (links != 0)
        {
            targets[links - 1] = (uint32_t)count;
        }
    }
    return count;
}
