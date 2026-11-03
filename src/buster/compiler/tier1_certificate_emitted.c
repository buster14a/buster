// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The certificate of the CFG consumer's code (#939). Included by tier1_emit.c after tier1_emit_cfg.c; it needs
// tier1_certificate.c and x86_semantics.c before it.
//
// tier1_certificate_cfg.c certifies tier1_cfg's allocation: its stream is the allocation's own account of its
// transitions. This file certifies the code the CFG consumer wrote from it. The stream is decoded from the emitted records,
// so the checker judges the concrete transitions: the stores, reloads and recomputations, each edge's moves wherever they
// were placed, the saves around calls and fixed-register rules, and the arguments' parallel moves. Each node's own records
// are regenerated from the allocation's registers and must equal the emitted ones byte for byte, so that its operands
// are read where the checker proves its values are.
//
// What is trusted, and what is checked:
// - Trusted, as the source: the IR and its selection (each node's rule, and what its sequence computes from the
//   registers it is given: tier1_emit_cfg_kernel, tier1_select_fixed), the ABI's argument registers, the
//   rematerialization recipes, the frame's layout of the frame base register's offsets, the CFG with its phis, and x86
//   semantics' effects of each record (which registers and memory it writes).
// - Untrusted: the allocation, every emitted record and the emitter's account of them: each record's origin
//   (Tier1Origin), each block's and stub's first record, and the references. An origin only says which decoding applies;
//   every record is decoded by its own bytes.
// - A move's record (mov, movss, movsd, movaps, kmov, between registers and the frame base's displacements) is the edit
//   its bytes say, at the width its form has; a recomputation is a constant's edit only where tier1_rematerialize writes
//   exactly that record (and its reference) for the value its origin names. A node's records are one ORIGINAL: its
//   operands read where the allocation has them at its use phase, every register and frame byte its records write
//   killed, its definition where the allocation's interval starts. A call's sequence is checked against what the IR says
//   it is (its target, al, the result's extension into its register), its arguments read in their ABI registers.
// - Control: each block's tail (its jumps and its edges' moves) is walked for each successor; every path must reach the
//   successor's first record through that edge's moves alone, or reach a successor whose only predecessor it is, whose
//   first records are that edge's moves. Every branch target is a block's or a stub's start.
// - The frame protocol: the prologue and each epilogue are decoded by rsp's offset from its value at entry, record by
//   record, each exactly its form's (push, the frame pointer's set, sub; add, the reset from rbp, pop), and must build
//   the frame the layout's offsets are of (its saved registers pushed, its frame pointer, its allocation exactly) with
//   rsp at a call a multiple of 16. A push is a store of its register's value into its slot, a pop the load back. Each
//   callee-saved register's value at entry and the return address are values of the certificate, asserted at every
//   block's entry, and each return's ret reads them where they were at entry, with rsp back at the return address: a
//   register the function doesn't give back, a slot something overwrote, an epilogue that pops the wrong slot or leaves
//   rsp elsewhere fails.
// - Frame storage: the frame base register's displacements over the spill, save and temporary rows are byte units, so a
//   slot that two values share over their lifetimes is the same bytes, and a node's store into a frame object over
//   them kills what it overwrites. A store through any other pointer is the program's own: it is assumed not to reach
//   the frame's rows, as C's object model says.
// - The prologue may not write any parameter's register, and no record but the prologue and the epilogues may write rsp
//   (but a call's and a ret's own push and pop), the frame base register or the frame pointer.
//
// Not decoded yet, so not certified, each counted by its reason (Tier1CertEmittedReason): by-value aggregates, stack
// parameters and arguments where the frame base register doesn't reach them, dynamic and realigned frames, a probe's
// prologue, a variadic definition's register save area, indirect jumps to several labels, and an entry block that is a
// loop's. The allocation's own certificate (tier1_certificate_cfg.c) still
// covers them. Canonical 32-bit storage (a 4-byte value's upper half zero) is the selection's contract and isn't
// checked: a 4-byte value is its low 4 bytes.
//
// Inputs are borrowed. The program is pushed onto `output`; `scratch` is restored. On failure both are restored.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum Tier1CertEmittedReason
{
    TIER1_CERT_EMITTED_OK,
    // The function has what the decoding doesn't cover: its code isn't certified.
    TIER1_CERT_EMITTED_AGGREGATE,
    TIER1_CERT_EMITTED_STACK,
    TIER1_CERT_EMITTED_DYNAMIC,
    TIER1_CERT_EMITTED_VARIADIC,
    TIER1_CERT_EMITTED_INDIRECT,
    TIER1_CERT_EMITTED_ENTRY_LOOP,
    TIER1_CERT_EMITTED_PROBE,
    // The code doesn't decode to its own certificate: a refusal.
    TIER1_CERT_EMITTED_ORIGIN,
    TIER1_CERT_EMITTED_DECODE,
    TIER1_CERT_EMITTED_KERNEL,
    TIER1_CERT_EMITTED_CONTROL,
    TIER1_CERT_EMITTED_FRAME,
    TIER1_CERT_EMITTED_REFERENCE,
    // The checker refused the decoded program (the report's error).
    TIER1_CERT_EMITTED_CHECK,
    // An arena couldn't hold the program or the checker's scratch.
    TIER1_CERT_EMITTED_CAPACITY,
    TIER1_CERT_EMITTED_REASON_COUNT,
} Tier1CertEmittedReason;

// The first reason that refuses the code, rather than leaving it uncertified.
#define TIER1_CERT_EMITTED_REFUSED TIER1_CERT_EMITTED_ORIGIN

static __attribute__((unused)) const char* tier1_cert_emitted_reason_name(Tier1CertEmittedReason reason)
{
    static const char* const names[TIER1_CERT_EMITTED_REASON_COUNT] = {
        "ok",
        "aggregate",
        "stack",
        "dynamic",
        "variadic",
        "indirect",
        "entry-loop",
        "probe",
        "origin",
        "decode",
        "kernel",
        "control",
        "frame",
        "reference",
        "check",
        "capacity",
    };
    return (uint32_t)reason < TIER1_CERT_EMITTED_REASON_COUNT ? names[reason] : "invalid";
}

typedef struct Tier1CertEmittedStats Tier1CertEmittedStats;
struct Tier1CertEmittedStats
{
    Tier1CertEmittedReason reason;
    // Where decoding refused the code: its record, or UINT32_MAX.
    uint32_t record;
    // The code's records, those decoded as edits (in blocks, and on edges) and the nodes' ORIGINALs.
    uint64_t records;
    uint64_t edits;
    uint64_t edge_edits;
    uint64_t originals;
    uint64_t sources;
    uint64_t facts;
    uint64_t locations;
    uint64_t units;
    // The published program's bytes.
    uint64_t bytes;
};

// The registers' locations: each general register's 8 and 4 bytes, each xmm register's 16, 8 and 4, each opmask
// register's 1, 2, 4 and 8, then the frame's, made as the stream reads them.
#define BUSTER_CERT_EMITTED_GPR 0u
#define BUSTER_CERT_EMITTED_XMM 32u
#define BUSTER_CERT_EMITTED_MASK 80u
#define BUSTER_CERT_EMITTED_REGISTERS 112u
#define BUSTER_CERT_EMITTED_FRAME_UNIT 448u
// A kernel's records past which the regeneration's room ends: a fixed rule's sequence, a call's and a node's.
#define BUSTER_CERT_EMITTED_KERNEL (BUSTER_TIER1_SELECT_FIXED_RECORDS + BUSTER_TIER1_SELECT_RECORDS_PER_NODE + 8)
// Source records per node at most: its uses' (up to 15 of a call's, in eights), its clobbers' (16 general, 16 xmm, 8
// opmask registers and a frame range, in eights) and its definition's.
#define BUSTER_CERT_EMITTED_NODE_SOURCES 10u

typedef struct Tier1CertEmittedBuild Tier1CertEmittedBuild;
struct Tier1CertEmittedBuild
{
    const Tier1SelectPlan* plan;
    const Tier1EmitResult* emitted;
    Tier1CertEmittedStats* stats;
    Tier1CertLocation* locations;
    uint32_t location_count;
    uint32_t location_capacity;
    // The frame base register's displacements [low, high) are units from BUSTER_CERT_EMITTED_FRAME_UNIT.
    int32_t low;
    int32_t high;
    uint8_t base;
    Tier1CertSource* source;
    uint32_t source_count;
    uint32_t source_capacity;
    Tier1CertInstruction* code;
    uint32_t code_count;
    uint32_t code_capacity;
    // Each record's reference, or UINT32_MAX; and whether a decoding took it.
    uint32_t* references;
    // The allocation's registers by node for a regeneration (tier1_cfg's location ids), UINT8_MAX elsewhere.
    uint8_t* claims;
    // Each record's branch, or UINT32_MAX.
    uint32_t* branch_of;
    uint32_t base_position;
    // The frame protocol (tier1_cert_emitted_prologue), in the stack pointer's offsets from its value at entry, where
    // the return address is: its value after the prologue, the frame base register's, the frame pointer's (rbp's, where
    // the prologue sets it, else INT64_MIN), and each register's push (INT64_MIN for one it doesn't push); and the stack
    // pointer as the stream's decoding has it.
    int64_t stack;
    int64_t frame_base;
    int64_t frame_pointer;
    int64_t saves[16];
    int64_t sp;
    // The first of the protocol's values past the function's: each callee-saved register's value at entry, then the
    // return address.
    uint32_t pseudo;
};

// The callee-saved registers, whose values at entry every return must give back, and the return address's value after
// them (Tier1CertEmittedBuild.pseudo).
static const uint8_t tier1_cert_emitted_callee[6] = { X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_R12, X86_REGISTER_R13, X86_REGISTER_R14, X86_REGISTER_R15 };
#define BUSTER_CERT_EMITTED_PSEUDO 7u

static bool tier1_cert_emitted_refuse(Tier1CertEmittedBuild* build, Tier1CertEmittedReason reason, uint32_t record)
{
    if (build->stats->reason == TIER1_CERT_EMITTED_OK)
    {
        build->stats->reason = reason;
        build->stats->record = record;
    }
    return false;
}

static uint32_t tier1_cert_emitted_gpr(uint32_t reg, uint32_t width)
{
    return BUSTER_CERT_EMITTED_GPR + 2 * (reg & 15) + (width == 8 ? 0 : 1);
}

static uint32_t tier1_cert_emitted_xmm(uint32_t reg, uint32_t width)
{
    return BUSTER_CERT_EMITTED_XMM + 3 * (reg & 15) + (width == 16 ? 0 : width == 8 ? 1 : 2);
}

static uint32_t tier1_cert_emitted_mask(uint32_t reg, uint32_t width)
{
    return BUSTER_CERT_EMITTED_MASK + 4 * (reg & 7) + (width == 1 ? 0 : width == 2 ? 1 : width == 4 ? 2 : 3);
}

// A value's width in its certificate: a general one's 8 bytes or 4 (every narrower one is in its 32-bit canonical form), a
// floating one's 4 or 8, a mask's own.
static uint16_t tier1_cert_emitted_width(const Tier1EmitResult* emitted, uint32_t value)
{
    const uint32_t node = emitted->value_nodes[value];
    const uint32_t class = emitted->cfg_input.classes[value];
    const uint32_t size = emitted->prepared.sizes[node];
    return class == BUSTER_TIER1_ALLOCATE_GPR ? (size == 8 ? 8 : 4) : (uint16_t)size;
}

// Value `value`'s location in tier1_cfg's register location `where` (class * 32 + register), at its width.
static uint32_t tier1_cert_emitted_register(const Tier1EmitResult* emitted, uint32_t value, uint32_t where)
{
    const uint32_t width = tier1_cert_emitted_width(emitted, value);
    const uint32_t class = where / 32;
    const uint32_t reg = where % 32;
    uint32_t result = BUSTER_CERT_NONE;
    if (class == BUSTER_TIER1_ALLOCATE_GPR && reg < 16)
    {
        result = tier1_cert_emitted_gpr(reg, width);
    }
    else if (class == BUSTER_TIER1_ALLOCATE_VECTOR && reg < 16 && (width == 4 || width == 8))
    {
        result = tier1_cert_emitted_xmm(reg, width);
    }
    else if (class == BUSTER_TIER1_ALLOCATE_MASK && reg < 8 && (width == 1 || width == 2 || width == 4 || width == 8))
    {
        result = tier1_cert_emitted_mask(reg, width);
    }
    return class == emitted->cfg_input.classes[value] ? result : BUSTER_CERT_NONE;
}

// The frame bytes [displacement, displacement + width) from the frame base register, as a new location, or
// BUSTER_CERT_NONE where they aren't all frame storage.
static uint32_t tier1_cert_emitted_frame(Tier1CertEmittedBuild* build, int64_t displacement, uint32_t width)
{
    uint32_t result = BUSTER_CERT_NONE;
    if (width != 0 && width <= BUSTER_CERT_MAX_WIDTH && displacement >= build->low && displacement + width <= build->high && build->location_count < build->location_capacity)
    {
        result = build->location_count++;
        build->locations[result] = (Tier1CertLocation){
            .unit = BUSTER_CERT_EMITTED_FRAME_UNIT + (uint32_t)(displacement - build->low),
            .width = (uint16_t)width,
            .extent = (uint16_t)width,
            .class = TIER1_CERT_STACK,
        };
    }
    return result;
}

// A record's frame operand: its memory operand at the frame base register's displacement, without an index or segment.
static bool tier1_cert_emitted_frame_operand(const Tier1CertEmittedBuild* build, X86Instruction record)
{
    return record.mode == BUSTER_X86_MEMORY_BASE && record.rm == build->base;
}

static bool tier1_cert_emitted_take_reference(Tier1CertEmittedBuild* build, uint32_t record, const LinkReference* expected)
{
    const uint32_t index = build->references[record];
    bool valid = (index == UINT32_MAX) == (expected == 0);
    if (valid && expected != 0)
    {
        const LinkReference found = build->emitted->references[index];
        valid = found.field == expected->field && found.kind == expected->kind && found.target == expected->target && found.addend == expected->addend;
    }
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_REFERENCE, record);
}

// Record `r`'s move, as an edit: its form's width, between its registers and frame bytes. A recomputation is decoded
// where `node` (its origin's value) has a recipe whose copy is exactly the record.
static bool tier1_cert_emitted_move(Tier1CertEmittedBuild* build, uint32_t r, uint32_t node, Tier1CertInstruction* edit)
{
    const Tier1EmitResult* emitted = build->emitted;
    const X86Instruction record = emitted->code.records[r];
    const bool direct = record.mode == BUSTER_X86_REGISTER_DIRECT;
    const bool frame = tier1_cert_emitted_frame_operand(build, record);
    uint32_t a = BUSTER_CERT_NONE;
    uint32_t b = BUSTER_CERT_NONE;
    // A general move: its load's or store's width, 8 or 4.
    const uint16_t form = record.form;
    const bool load64 = form == X86_FORM_MOV_R64_RM64;
    const bool load32 = form == X86_FORM_MOV_R32_RM32;
    const bool store64 = form == X86_FORM_MOV_RM64_R64;
    const bool store32 = form == X86_FORM_MOV_RM32_R32;
    if ((load64 || load32) && (direct || frame) && record.reg < 16)
    {
        const uint32_t width = load64 ? 8 : 4;
        a = tier1_cert_emitted_gpr(record.reg, width);
        b = direct ? (record.rm < 16 ? tier1_cert_emitted_gpr(record.rm, width) : BUSTER_CERT_NONE) : tier1_cert_emitted_frame(build, record.displacement, width);
    }
    else if ((store64 || store32) && (direct || frame) && record.reg < 16)
    {
        const uint32_t width = store64 ? 8 : 4;
        a = direct ? (record.rm < 16 ? tier1_cert_emitted_gpr(record.rm, width) : BUSTER_CERT_NONE) : tier1_cert_emitted_frame(build, record.displacement, width);
        b = tier1_cert_emitted_gpr(record.reg, width);
    }
    else if (form == X86_FORM_MOVAPS_X_XM && direct && record.reg < 16 && record.rm < 16)
    {
        a = tier1_cert_emitted_xmm(record.reg, 16);
        b = tier1_cert_emitted_xmm(record.rm, 16);
    }
    else if ((form == X86_FORM_MOVSS_X_XM32 || form == X86_FORM_MOVSD_X_XM64) && frame && record.reg < 16)
    {
        // A load from memory zeroes the rest of the register: its scalar view's extent.
        const uint32_t width = form == X86_FORM_MOVSS_X_XM32 ? 4 : 8;
        a = tier1_cert_emitted_xmm(record.reg, width);
        b = tier1_cert_emitted_frame(build, record.displacement, width);
    }
    else if ((form == X86_FORM_MOVSS_XM32_X || form == X86_FORM_MOVSD_XM64_X) && frame && record.reg < 16)
    {
        const uint32_t width = form == X86_FORM_MOVSS_XM32_X ? 4 : 8;
        a = tier1_cert_emitted_frame(build, record.displacement, width);
        b = tier1_cert_emitted_xmm(record.reg, width);
    }
    else
    {
        // An opmask register's kmov of its width: between opmask registers, or from and to the frame.
        for (uint32_t width = 1; width <= 8; width *= 2)
        {
            const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
            if (form == forms->move && direct && record.reg < 8 && record.rm < 8)
            {
                a = tier1_cert_emitted_mask(record.reg, width);
                b = tier1_cert_emitted_mask(record.rm, width);
            }
            else if (form == forms->load && frame && record.reg < 8)
            {
                a = tier1_cert_emitted_mask(record.reg, width);
                b = tier1_cert_emitted_frame(build, record.displacement, width);
            }
            else if (form == forms->store && frame && record.reg < 8)
            {
                a = tier1_cert_emitted_frame(build, record.displacement, width);
                b = tier1_cert_emitted_mask(record.reg, width);
            }
        }
    }
    // A move's fields but its registers, its mode and a memory operand's displacement are zero.
    const bool exact = record.immediate == 0 && record.index == 0 && record.vvvv == 0 && record.decorations == 0 && (!direct || record.displacement == 0);
    bool valid = a != BUSTER_CERT_NONE && b != BUSTER_CERT_NONE && exact;
    if (valid)
    {
        *edit = (Tier1CertInstruction){
            .a = a,
            .b = b,
            .op = TIER1_CERT_MOVE,
        };
        valid = tier1_cert_emitted_take_reference(build, r, 0);
    }
    else
    {
        // A recomputation into a general register: the recipe's copy of the value its origin names, into the register the
        // record writes.
        const uint32_t value = node < build->plan->function->instruction_count ? emitted->node_values[node] : BUSTER_TIER1_CFG_NONE;
        const bool recipe = value != BUSTER_TIER1_CFG_NONE && emitted->cfg_input.rematerializable != 0 && emitted->cfg_input.rematerializable[value] != 0 && emitted->cfg_input.classes[value] == BUSTER_TIER1_ALLOCATE_GPR;
        const uint8_t candidates[2] = { record.reg, record.rm };
        for (uint32_t k = 0; recipe && !valid && k < 2; k += 1)
        {
            Tier1Rematerialized copy = { 0 };
            if (candidates[k] < 16 && tier1_rematerialize(&emitted->prepared.recipes, node, candidates[k], build->base, BUSTER_TIER1_EMIT_BOUNDARY_FLAGS, &copy) && memcmp(&copy.instruction, &record, sizeof(record)) == 0)
            {
                const LinkReference expected = {
                    .field = X86_FIXUP_DISPLACEMENT,
                    .kind = copy.got ? BUSTER_LINK_FIXUP_LOAD_GOT : BUSTER_LINK_FIXUP_PC32,
                    .target = copy.symbol,
                    .addend = copy.addend,
                };
                valid = tier1_cert_emitted_take_reference(build, r, copy.symbol != BUSTER_TIER1_REMAT_NO_SYMBOL ? &expected : 0);
                *edit = (Tier1CertInstruction){
                    .immediate = value,
                    .a = tier1_cert_emitted_gpr(candidates[k], tier1_cert_emitted_width(emitted, value)),
                    .value = value,
                    .op = TIER1_CERT_CONSTANT,
                };
            }
        }
        if (!valid && build->stats->reason == TIER1_CERT_EMITTED_OK)
        {
            tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_DECODE, r);
        }
    }
    // No move writes rsp, nor rbp while it is the frame pointer: the frame protocol's decoding follows them.
    const Tier1CertLocation written = valid ? build->locations[edit->a] : (Tier1CertLocation){ 0 };
    const bool protocol = valid && written.class == TIER1_CERT_GPR && (written.reg == X86_REGISTER_RSP || (written.reg == X86_REGISTER_RBP && build->frame_pointer != INT64_MIN));
    valid = valid && (!protocol || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r));
    return valid;
}

// Value v's location at `position` as the allocation has it in a register (tier1_cfg's location id), or UINT32_MAX.
static uint32_t tier1_cert_emitted_use(const Tier1EmitResult* emitted, uint32_t value, uint32_t position)
{
    uint32_t where = 0;
    uint8_t kind = 0;
    const bool found = value != BUSTER_TIER1_CFG_NONE && tier1_cfg_location(&emitted->cfg, value, position, &where, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
    return found ? where : UINT32_MAX;
}

// Node `node`'s definition's register, where the allocation's interval starting at its definition phase has it, or
// UINT32_MAX.
static uint32_t tier1_cert_emitted_definition(const Tier1EmitResult* emitted, uint32_t node, uint32_t position)
{
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint32_t value = emitted->node_values[node];
    const uint32_t interval = value != BUSTER_TIER1_CFG_NONE ? tier1_cfg_interval_at(cfg, value, position) : BUSTER_TIER1_CFG_NONE;
    const uint32_t fragment = interval != BUSTER_TIER1_CFG_NONE ? cfg->allocation.fragment_offsets[interval] : 0;
    const bool found = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == position && cfg->allocation.kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER;
    return found ? 32 * emitted->cfg_input.classes[value] + cfg->allocation.registers[fragment] : UINT32_MAX;
}

// A node's expected records, its operands' locations and its definition's: what the IR and its selection make of the
// allocation's registers.
typedef struct Tier1CertEmittedKernel Tier1CertEmittedKernel;
struct Tier1CertEmittedKernel
{
    X86Instruction records[BUSTER_CERT_EMITTED_KERNEL];
    LinkReference reference;
    uint32_t count;
    // The record that has `reference`, or UINT32_MAX.
    uint32_t referenced;
    // A conditional branch's condition's form (its comparison's jcc), else X86_FORM_NONE.
    uint16_t condition;
    uint32_t uses[BUSTER_TIER1_SELECT_MAX_ARGUMENTS + 2];
    uint32_t use_locations[BUSTER_TIER1_SELECT_MAX_ARGUMENTS + 2];
    uint32_t use_count;
    uint32_t definition;
    uint32_t definition_location;
    // A call's outgoing area's bytes from rsp, which its callee owns: killed after it.
    uint32_t outgoing;
};

static bool tier1_cert_emitted_add_use(Tier1CertEmittedKernel* kernel, uint32_t value, uint32_t location)
{
    bool fresh = true;
    for (uint32_t k = 0; fresh && k < kernel->use_count; k += 1)
    {
        fresh = kernel->uses[k] != value;
    }
    if (fresh && kernel->use_count < BUSTER_ARRAY_LENGTH(kernel->uses))
    {
        kernel->uses[kernel->use_count] = value;
        kernel->use_locations[kernel->use_count] = location;
        kernel->use_count += 1;
    }
    return location != BUSTER_CERT_NONE && (!fresh || kernel->use_count <= BUSTER_ARRAY_LENGTH(kernel->uses));
}

// A call's expected sequence (tier1_emit_call's, for a call without aggregates or stack arguments): a variadic call's
// al, the call, and its result into its register, extended to its canonical form; its register arguments read in their
// ABI registers, an indirect call's callee in r10.
static bool tier1_cert_emitted_call(Tier1CertEmittedBuild* build, uint32_t node, uint32_t destination, Tier1CertEmittedKernel* kernel)
{
    const IrFunction* function = build->plan->function;
    const Tier1EmitResult* emitted = build->emitted;
    const IrCall* call = &function->calls[function->b[node]];
    const IrAbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1] : 0;
    const bool indirect = function->op[node] == IR_CALL_INDIRECT;
    const uint32_t offset = build->plan->effective_use_offsets[node];
    bool valid = build->plan->effective_use_offsets[node + 1] - offset == call->argument_count + (indirect ? 1u : 0u);
    Tier1SelectPlaces places = { 0 };
    uint32_t vectors = 0;
    for (uint32_t argument = 0; valid && argument < call->argument_count; argument += 1)
    {
        const Tier1SelectPlace place = tier1_select_argument_place(function, call, argument, &places);
        const uint32_t value = emitted->node_values[build->plan->effective_use_values[offset + argument]];
        valid = value != BUSTER_TIER1_CFG_NONE && (place.kind == BUSTER_TIER1_PLACE_GENERAL || place.kind == BUSTER_TIER1_PLACE_VECTOR || place.kind == BUSTER_TIER1_PLACE_STACK);
        const uint32_t where = place.kind == BUSTER_TIER1_PLACE_GENERAL ? 32 * BUSTER_TIER1_ALLOCATE_GPR + tier1_select_parameter_registers[place.index] : 32 * BUSTER_TIER1_ALLOCATE_VECTOR + place.index;
        // An argument past its class's registers is read from its 8-byte slot in the outgoing area, at rsp: its own bytes.
        const uint32_t location = place.kind == BUSTER_TIER1_PLACE_STACK ? tier1_cert_emitted_frame(build, (int64_t)BUSTER_ABI_SLOT_BYTES * place.index, tier1_cert_emitted_width(emitted, value)) : tier1_cert_emitted_register(emitted, value, where);
        valid = valid && tier1_cert_emitted_add_use(kernel, value, location);
        vectors += place.kind == BUSTER_TIER1_PLACE_VECTOR;
    }
    if (valid && indirect)
    {
        const uint32_t value = emitted->node_values[build->plan->effective_use_values[offset + call->argument_count]];
        valid = value != BUSTER_TIER1_CFG_NONE && tier1_cert_emitted_add_use(kernel, value, tier1_cert_emitted_register(emitted, value, X86_REGISTER_R10));
    }
    const uint32_t vector_count = abi != 0 ? abi->call.vector_registers : vectors;
    kernel->outgoing = tier1_select_call_stack_bytes(function, call);
    if (valid && call->variadic)
    {
        kernel->records[kernel->count++] = vector_count == 0 ? (X86Instruction){ .form = X86_FORM_XOR_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX }
                                                             : (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RAX, .immediate = (int32_t)vector_count };
    }
    if (valid && indirect)
    {
        kernel->records[kernel->count++] = (X86Instruction){ .form = X86_FORM_CALL_RM64, .rm = X86_REGISTER_R10 };
    }
    else if (valid)
    {
        kernel->reference = (LinkReference){
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_CALL,
            .target = function->a[node],
            .addend = 0,
        };
        kernel->referenced = kernel->count;
        kernel->records[kernel->count++] = (X86Instruction){ .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP };
    }
    // The result, by its type: a floating one from xmm0, a narrow one zero-extended from al or ax, an I32 by a 32-bit move
    // (which zero-extends), a 64-bit one moved where it isn't in rax.
    const uint32_t result_type = function->type[node];
    const uint32_t narrow = result_type == IR_I1 || result_type == IR_I8 ? 1 : result_type == IR_I16 ? 2 : 0;
    const uint32_t reg = destination % 32;
    if (valid && result_type != IR_VOID)
    {
        valid = destination != UINT32_MAX;
        if (valid && tier1_select_float_width(result_type) != 0)
        {
            valid = destination / 32 == BUSTER_TIER1_ALLOCATE_VECTOR && reg < 16;
            if (valid && reg != 0)
            {
                kernel->records[kernel->count++] = tier1_select_float_move((uint8_t)reg, 0);
            }
        }
        else if (valid)
        {
            valid = destination / 32 == BUSTER_TIER1_ALLOCATE_GPR && reg < 16;
            if (valid && narrow != 0)
            {
                kernel->records[kernel->count++] = (X86Instruction){ .form = narrow == 1 ? X86_FORM_MOVZX_R32_RM8 : X86_FORM_MOVZX_R32_RM16, .reg = (uint8_t)reg, .rm = X86_REGISTER_RAX };
            }
            else if (valid && (reg != X86_REGISTER_RAX || result_type == IR_I32))
            {
                kernel->records[kernel->count++] = tier1_emit_move(X86_REGISTER_RAX, reg, tier1_select_width(result_type));
            }
        }
    }
    return valid;
}

// Node `node`'s expected records and operands, or false where the allocation doesn't give them registers.
static bool tier1_cert_emitted_kernel(Tier1CertEmittedBuild* build, uint32_t node, Tier1CertEmittedKernel* kernel)
{
    const Tier1SelectPlan* plan = build->plan;
    const IrFunction* function = plan->function;
    const Tier1EmitResult* emitted = build->emitted;
    const uint32_t use = build->base_position + 2 * node;
    const uint32_t op = function->op[node];
    const uint32_t value = emitted->node_values[node];
    const bool defines = function->type[node] != IR_VOID && value != BUSTER_TIER1_CFG_NONE;
    const uint32_t destination = defines ? tier1_cert_emitted_definition(emitted, node, use + 1) : UINT32_MAX;
    *kernel = (Tier1CertEmittedKernel){
        .referenced = UINT32_MAX,
        .condition = X86_FORM_NONE,
        .definition = defines ? value : BUSTER_TIER1_CFG_NONE,
        .definition_location = defines && destination != UINT32_MAX ? tier1_cert_emitted_register(emitted, value, destination) : BUSTER_CERT_NONE,
    };
    bool valid = !defines || kernel->definition_location != BUSTER_CERT_NONE;
    if (valid && (op == IR_CALL || op == IR_CALL_INDIRECT))
    {
        valid = tier1_cert_emitted_call(build, node, destination, kernel);
    }
    else if (valid)
    {
        // The operands' registers, as the selection reads them (tier1_select_holes): its pattern's values and its own.
        const Tier1SelectPattern* pattern = &plan->patterns[node];
        for (uint32_t p = 0; valid && p < 3; p += 1)
        {
            const uint32_t operand = pattern->values[p];
            const uint32_t operand_value = operand != BUSTER_IR_NONE ? emitted->node_values[operand] : BUSTER_TIER1_CFG_NONE;
            const uint32_t where = operand != BUSTER_IR_NONE ? tier1_cert_emitted_use(emitted, operand_value, use) : 0;
            valid = operand == BUSTER_IR_NONE || (where != UINT32_MAX && tier1_cert_emitted_add_use(kernel, operand_value, tier1_cert_emitted_register(emitted, operand_value, where)));
            if (valid && operand != BUSTER_IR_NONE)
            {
                build->claims[operand] = (uint8_t)where;
            }
        }
        if (valid && defines)
        {
            build->claims[node] = (uint8_t)destination;
        }
        const Tier1SelectHoles holes = tier1_emit_cfg_holes(plan, emitted, build->claims, node);
        const uint32_t successors = emitted->cfg_input.successor_offsets[tier1_cfg_block_of(&emitted->cfg_input, use) + 1] - emitted->cfg_input.successor_offsets[tier1_cfg_block_of(&emitted->cfg_input, use)];
        if (valid && tier1_select_fixed_registers(pattern->rule) != 0)
        {
            kernel->count = tier1_select_fixed(&holes, kernel->records);
            valid = kernel->count != 0;
        }
        else if (valid)
        {
            Tier1SelectRecords records = { 0 };
            bool referenced = false;
            valid = tier1_emit_cfg_kernel(plan, emitted, &holes, node, &records, &kernel->reference, &referenced) == TIER1_EMIT_OK;
            // The immediate's form, as the function's batch chooses it: the scalar twin's choice is the batch's.
            if (valid && records.requested)
            {
                uint16_t form = X86_FORM_NONE;
                x86_choose_forms_scalar(&records.row, &records.value, &records.context, 1, &form);
                records.records[records.request] = form != X86_FORM_NONE ? x86_choice_instruction(form, (X86Instruction){ .rm = records.destination }, records.value) : (X86Instruction){ 0 };
            }
            uint32_t count = records.count;
            // A conditional branch with two successors keeps its comparison: its last two records are the consumer's own
            // jumps, the first of them its condition's.
            if (valid && op == IR_CBR && successors == 2)
            {
                valid = count >= 2;
                count = valid ? count - 2 : 0;
                kernel->condition = valid ? records.records[count].form : X86_FORM_NONE;
            }
            else if (op == IR_BR || ((op == IR_CBR || op == IR_BR_INDIRECT) && successors == 1))
            {
                count = 0;
            }
            for (uint32_t r = 0; valid && r < count; r += 1)
            {
                kernel->records[kernel->count++] = records.records[r];
            }
            kernel->referenced = referenced ? 0 : UINT32_MAX;
        }
        for (uint32_t p = 0; p < 3; p += 1)
        {
            if (pattern->values[p] != BUSTER_IR_NONE)
            {
                build->claims[pattern->values[p]] = UINT8_MAX;
            }
        }
        build->claims[node] = UINT8_MAX;
    }
    return valid;
}

// A source instruction and its ORIGINAL: `kind` its part (0 uses, 1 clobbers, 2 the definition), `chunk` its index.
static bool tier1_cert_emitted_original(Tier1CertEmittedBuild* build, uint32_t node, uint32_t kind, uint32_t chunk, Tier1CertSource** record, Tier1CertInstruction** instruction)
{
    const bool room = build->source_count < build->source_capacity && build->code_count < build->code_capacity;
    if (room)
    {
        *record = build->source + build->source_count++;
        *instruction = build->code + build->code_count++;
        **record = (Tier1CertSource){
            .immediate = chunk,
            .effect = kind,
            .opcode = node,
        };
        **instruction = (Tier1CertInstruction){
            .immediate = chunk,
            .effect = kind,
            .opcode = node,
            .op = TIER1_CERT_ORIGINAL,
        };
        build->stats->sources += 1;
    }
    return room;
}

// Node `node`'s ORIGINALs, its records [first, first + count) its kernel's: its uses in eights, then every register and
// frame range its records write, killed in eights, then its definition.
static bool tier1_cert_emitted_originals(Tier1CertEmittedBuild* build, uint32_t node, const Tier1CertEmittedKernel* kernel, const X86Instruction* records, uint32_t count)
{
    uint64_t writes = 0;
    uint16_t masks = 0;
    uint32_t clobbers[16 + 16 + 8 + BUSTER_CERT_EMITTED_KERNEL];
    uint32_t clobber_count = 0;
    bool valid = true;
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        uint64_t read = 0;
        uint64_t written = 0;
        uint16_t opmasks = 0;
        x86_instruction_effects_opmasks_scalar(&records[r], &read, &written, &opmasks);
        writes |= written;
        masks |= opmasks;
        // No record writes the frame base register but the prologue and the epilogues, nor rsp but a call's and a ret's
        // own push and pop.
        const bool control = (written & (BUSTER_X86_RESOURCE_CALL | BUSTER_X86_RESOURCE_CONTROL)) != 0;
        valid = ((written & BUSTER_X86_RESOURCE_GPR(build->base)) == 0 || (build->base == X86_REGISTER_RSP && control)) && ((written & BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RSP)) == 0 || control);
        valid = valid && (build->frame_pointer == INT64_MIN || (written & BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBP)) == 0);
        valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, UINT32_MAX);
        // A store into the frame kills the bytes it writes that are the frame's storage; a write the semantics doesn't
        // bound kills them all.
        if ((written & BUSTER_X86_RESOURCE_MEMORY) != 0 && tier1_cert_emitted_frame_operand(build, records[r]))
        {
            const X86FormDescription* description = x86_form_description(records[r].form & (BUSTER_X86_FORM_FIXUP - 1));
            const int64_t start = records[r].displacement;
            const int64_t end = start + (description->operand_bytes != 0 ? description->operand_bytes : BUSTER_CERT_MAX_WIDTH);
            const int64_t low = start > build->low ? start : build->low;
            const int64_t high = end < build->high ? end : build->high;
            for (int64_t at = low; valid && at < high; at += BUSTER_CERT_MAX_WIDTH)
            {
                // Room for the registers' clobbers after it.
                const uint32_t width = (uint32_t)(high - at < BUSTER_CERT_MAX_WIDTH ? high - at : BUSTER_CERT_MAX_WIDTH);
                clobbers[clobber_count] = clobber_count + 40 < BUSTER_ARRAY_LENGTH(clobbers) ? tier1_cert_emitted_frame(build, at, width) : BUSTER_CERT_NONE;
                valid = clobbers[clobber_count] != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX);
                clobber_count += 1;
            }
        }
    }
    // A call's callee owns its outgoing area.
    for (uint32_t at = 0; valid && at < kernel->outgoing; at += BUSTER_CERT_MAX_WIDTH)
    {
        const uint32_t width = kernel->outgoing - at < BUSTER_CERT_MAX_WIDTH ? kernel->outgoing - at : BUSTER_CERT_MAX_WIDTH;
        clobbers[clobber_count] = clobber_count + 40 < BUSTER_ARRAY_LENGTH(clobbers) ? tier1_cert_emitted_frame(build, at, width) : BUSTER_CERT_NONE;
        valid = clobbers[clobber_count] != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX);
        clobber_count += 1;
    }
    for (uint32_t reg = 0; valid && reg < 16; reg += 1)
    {
        if (reg != X86_REGISTER_RSP && (writes & BUSTER_X86_RESOURCE_GPR(reg)) != 0)
        {
            clobbers[clobber_count++] = tier1_cert_emitted_gpr(reg, 8);
        }
        if ((writes & BUSTER_X86_RESOURCE_VECTOR(reg)) != 0)
        {
            clobbers[clobber_count++] = tier1_cert_emitted_xmm(reg, 16);
        }
        if (reg < 8 && ((masks >> 8 >> reg) & 1) != 0)
        {
            clobbers[clobber_count++] = tier1_cert_emitted_mask(reg, 8);
        }
    }
    Tier1CertSource* record = 0;
    Tier1CertInstruction* instruction = 0;
    uint32_t chunk = 0;
    const bool defines = kernel->definition != BUSTER_TIER1_CFG_NONE;
    // One record where its uses, its definition and its clobbers fit one: the checker reads the uses, kills the clobbers,
    // then writes the definition, as the records in turn would.
    const bool single = kernel->use_count + defines <= BUSTER_CERT_MAX_OPERANDS && clobber_count <= BUSTER_CERT_MAX_OPERANDS;
    if (valid && single)
    {
        valid = tier1_cert_emitted_original(build, node, 4, 0, &record, &instruction);
        for (uint32_t k = 0; valid && k < kernel->use_count; k += 1)
        {
            record->operands[k] = (Tier1CertOperand){
                .value = kernel->uses[k],
                .allowed_registers = UINT32_MAX,
                .flags = build->locations[kernel->use_locations[k]].class == TIER1_CERT_STACK ? BUSTER_CERT_MEMORY : 0,
            };
            instruction->locations[k] = kernel->use_locations[k];
        }
        for (uint32_t k = 0; valid && k < clobber_count; k += 1)
        {
            record->after[k] = clobbers[k];
        }
        if (valid && defines)
        {
            record->operands[kernel->use_count] = (Tier1CertOperand){
                .value = kernel->definition,
                .allowed_registers = UINT32_MAX,
            };
            instruction->locations[kernel->use_count] = kernel->definition_location;
        }
        if (valid)
        {
            record->use_count = (uint8_t)kernel->use_count;
            record->def_count = defines;
            record->after_count = (uint8_t)clobber_count;
            instruction->operand_count = (uint8_t)(kernel->use_count + defines);
        }
    }
    for (uint32_t u = 0; valid && !single && u < kernel->use_count; u += BUSTER_CERT_MAX_OPERANDS)
    {
        valid = tier1_cert_emitted_original(build, node, 0, chunk++, &record, &instruction);
        for (uint32_t k = u; valid && k < kernel->use_count && k < u + BUSTER_CERT_MAX_OPERANDS; k += 1)
        {
            record->operands[record->use_count] = (Tier1CertOperand){
                .value = kernel->uses[k],
                .allowed_registers = UINT32_MAX,
                .flags = build->locations[kernel->use_locations[k]].class == TIER1_CERT_STACK ? BUSTER_CERT_MEMORY : 0,
            };
            instruction->locations[record->use_count] = kernel->use_locations[k];
            record->use_count += 1;
        }
        instruction->operand_count = valid ? record->use_count : 0;
    }
    for (uint32_t c = 0; valid && !single && c < clobber_count; c += BUSTER_CERT_MAX_OPERANDS)
    {
        valid = tier1_cert_emitted_original(build, node, 1, chunk++, &record, &instruction);
        for (uint32_t k = c; valid && k < clobber_count && k < c + BUSTER_CERT_MAX_OPERANDS; k += 1)
        {
            record->after[record->after_count++] = clobbers[k];
        }
    }
    if (valid && !single && defines)
    {
        valid = tier1_cert_emitted_original(build, node, 2, chunk++, &record, &instruction);
        if (valid)
        {
            record->operands[0] = (Tier1CertOperand){
                .value = kernel->definition,
                .allowed_registers = UINT32_MAX,
            };
            record->def_count = 1;
            instruction->locations[0] = kernel->definition_location;
            instruction->operand_count = 1;
        }
    }
    build->stats->originals += valid;
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX);
}


// Whether node `node` may have no records of its own: one the selection covers, a phi or the entry's parameter; a jump,
// whose records are its block's tail; or a recomputed value's definition that nothing reads in its register (the
// consumer leaves it out, and its readers' recomputations stand for it).
static bool tier1_cert_emitted_silent(const Tier1EmitResult* emitted, const Tier1SelectPlan* plan, uint32_t node)
{
    const IrFunction* function = plan->function;
    const uint32_t op = function->op[node];
    const uint32_t value = emitted->node_values[node];
    const bool selected = !plan->covered[node] && op != IR_PHI && !tier1_select_entry_parameter(function, node);
    const bool recomputed = value != BUSTER_TIER1_CFG_NONE && emitted->cfg_input.rematerializable != 0 && emitted->cfg_input.rematerializable[value] != 0;
    return !selected || op == IR_BR || op == IR_CBR || op == IR_BR_INDIRECT || recomputed;
}

static Tier1Origin tier1_cert_emitted_kind(uint32_t origin)
{
    return (Tier1Origin)(origin >> BUSTER_TIER1_ORIGIN_SHIFT);
}

// The function's shape that the decoding doesn't cover, by reason, or TIER1_CERT_EMITTED_OK.
static Tier1CertEmittedReason tier1_cert_emitted_supported(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    Tier1CertEmittedReason reason = TIER1_CERT_EMITTED_OK;
    if (emitted->frame.dynamic_outgoing || emitted->frame.realigned)
    {
        reason = TIER1_CERT_EMITTED_DYNAMIC;
    }
    else if (emitted->frame.variadic)
    {
        reason = TIER1_CERT_EMITTED_VARIADIC;
    }
    else if (function->result_size != 0)
    {
        reason = TIER1_CERT_EMITTED_AGGREGATE;
    }
    else if (input->predecessor_offsets[1] != input->predecessor_offsets[0])
    {
        reason = TIER1_CERT_EMITTED_ENTRY_LOOP;
    }
    // A probe's loop in the prologue isn't decoded.
    for (uint32_t k = 0; reason == TIER1_CERT_EMITTED_OK && k < emitted->frame.prologue_count && k < BUSTER_TIER1_FRAME_STEP_CAPACITY; k += 1)
    {
        const Tier1FrameStepKind kind = emitted->frame.prologue[k].kind;
        reason = kind == TIER1_FRAME_PROBE_ALLOCATE || kind == TIER1_FRAME_PROBE_ALIGN ? TIER1_CERT_EMITTED_PROBE : reason;
    }
    for (uint32_t node = 0; reason == TIER1_CERT_EMITTED_OK && node < function->instruction_count; node += 1)
    {
        const uint32_t op = function->op[node];
        const uint32_t rule = plan->patterns[node].rule;
        if (rule == TIER1_RULE_ALLOCA || rule == TIER1_RULE_ALLOCA_IMMEDIATE || rule == TIER1_RULE_STACK_RESTORE)
        {
            reason = TIER1_CERT_EMITTED_DYNAMIC;
        }
        else if (op == IR_PARAM && function->parameter_passed != 0 && tier1_select_parameter_record(function, function->a[node]) != 0 && tier1_select_parameter_record(function, function->a[node])->size != 0)
        {
            reason = TIER1_CERT_EMITTED_AGGREGATE;
        }
        else if (op == IR_PARAM && tier1_select_entry_parameter(function, node) && tier1_select_parameter_place(function, function->a[node]).kind != BUSTER_TIER1_PLACE_GENERAL &&
                 tier1_select_parameter_place(function, function->a[node]).kind != BUSTER_TIER1_PLACE_VECTOR)
        {
            // A parameter on the stack, in the incoming area, which the frame base register must reach.
            const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
            uint8_t base = 0;
            int32_t offset = 0;
            const bool reached = place.kind == BUSTER_TIER1_PLACE_STACK && tier1_emit_cfg_incoming(emitted, (uint64_t)BUSTER_ABI_SLOT_BYTES * place.index, &base, &offset) && base == emitted->frame.base_register;
            reason = reached ? reason : place.kind == BUSTER_TIER1_PLACE_STACK ? TIER1_CERT_EMITTED_STACK : TIER1_CERT_EMITTED_AGGREGATE;
        }
        else if ((op == IR_CALL || op == IR_CALL_INDIRECT) && !plan->covered[node])
        {
            // A call's stack arguments go in its outgoing area from rsp, which must be the frame base register.
            const IrCall* call = &function->calls[function->b[node]];
            const IrAbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1] : 0;
            Tier1SelectPlaces places = { 0 };
            reason = tier1_select_call_stack_bytes(function, call) != 0 && emitted->frame.base_register != X86_REGISTER_RSP ? TIER1_CERT_EMITTED_STACK : abi != 0 && abi->result_size != 0 ? TIER1_CERT_EMITTED_AGGREGATE : reason;
            for (uint32_t argument = 0; reason == TIER1_CERT_EMITTED_OK && argument < call->argument_count; argument += 1)
            {
                const Tier1SelectPlace place = tier1_select_argument_place(function, call, argument, &places);
                reason = place.kind == BUSTER_TIER1_PLACE_GENERAL || place.kind == BUSTER_TIER1_PLACE_VECTOR || place.kind == BUSTER_TIER1_PLACE_STACK ? reason : TIER1_CERT_EMITTED_AGGREGATE;
            }
        }
    }
    const uint32_t added = input->block_count - function->block_count;
    for (uint32_t b = added; reason == TIER1_CERT_EMITTED_OK && b < input->block_count; b += 1)
    {
        const uint32_t last = function->block_offsets[b - added + 1];
        const bool indirect = last != 0 && function->op[last - 1] == IR_BR_INDIRECT;
        reason = indirect && input->successor_offsets[b + 1] - input->successor_offsets[b] > 1 ? TIER1_CERT_EMITTED_INDIRECT : reason;
    }
    return reason;
}

// Block `b`'s tail, walked for its edge `e` (`outcome` the condition's value for a conditional branch's, its form
// `condition`): from `start`, through its jumps and its edges' moves, to its successor's first record. True where it
// gets there through edge e's moves alone; `taken` counts them. A path may leave the block only by a jump or past its end,
// and arrives where it reaches the successor's first record having left `start`, or outside the tail.
static bool tier1_cert_emitted_walk(Tier1CertEmittedBuild* build, uint32_t b, uint32_t e, bool outcome, uint16_t condition, uint32_t start, uint32_t* taken)
{
    const Tier1EmitResult* emitted = build->emitted;
    const uint32_t target = emitted->block_records[emitted->cfg_input.successors[e]];
    const uint32_t end = emitted->block_records[b + 1];
    const uint16_t inverse = condition != X86_FORM_NONE ? tier1_emit_cfg_inverse(condition) : X86_FORM_NONE;
    uint32_t pc = start;
    bool valid = true;
    bool arrived = false;
    *taken = 0;
    // A path that arrives visits each of the tail's records once at most.
    for (uint32_t steps = 0; valid && !arrived && steps <= end - start + 1; steps += 1)
    {
        arrived = pc == target && (steps != 0 || pc < start || pc >= end);
        valid = arrived || (pc >= start && pc < end);
        const uint32_t origin = valid && !arrived ? emitted->origins[pc] : 0;
        const Tier1Origin kind = tier1_cert_emitted_kind(origin);
        if (valid && !arrived && kind == TIER1_ORIGIN_EDGE)
        {
            valid = (origin & BUSTER_TIER1_ORIGIN_PAYLOAD) == e;
            *taken += 1;
            pc += 1;
        }
        else if (valid && !arrived && kind == TIER1_ORIGIN_CONTROL)
        {
            const uint16_t form = emitted->code.records[pc].form;
            const uint32_t branch = build->branch_of[pc];
            const bool jump = form == X86_FORM_JMP_REL8 || form == X86_FORM_JMP_REL32;
            const bool conditional = condition != X86_FORM_NONE && (form == condition || form == inverse);
            const bool branches = jump || (form == condition && outcome) || (form == inverse && !outcome);
            valid = branch != UINT32_MAX && (jump || conditional);
            pc = valid ? (branches ? emitted->code.targets[branch] : pc + 1) : pc;
        }
        else if (valid && !arrived)
        {
            valid = false;
        }
    }
    return (valid && arrived) || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_CONTROL, pc);
}

// Whether `record` is exactly `expected`: a frame record's every field its decoding's, the rest zero.
static bool tier1_cert_emitted_exactly(X86Instruction record, X86Instruction expected)
{
    return memcmp(&record, &expected, sizeof(record)) == 0;
}

// An immediate's form for the frame's add and sub: its imm8 one where the value fits it, else its imm32 one.
static bool tier1_cert_emitted_immediate_form(uint16_t form, uint16_t imm8, uint16_t imm32, int32_t value)
{
    return (form == imm8 && value >= INT8_MIN && value <= INT8_MAX) || form == imm32;
}

// The prologue, its records from the first (origin FRAME 0), decoded by the stack pointer's offset from its value at
// entry: a push of a general register (each register once), the frame pointer's set from rsp (once), an allocation by
// sub. Nothing else may be in it: a probe's loop or a realignment is a shape left uncertified before this. It gives the
// frame base register's offset, rsp's after it, and where each push put its register. `end` is the record after it.
static bool tier1_cert_emitted_prologue(Tier1CertEmittedBuild* build, uint32_t* end)
{
    const Tier1EmitResult* emitted = build->emitted;
    const uint32_t prologue = tier1_origin(TIER1_ORIGIN_FRAME, 0);
    int64_t sp = 0;
    bool valid = true;
    build->frame_pointer = INT64_MIN;
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        build->saves[reg] = INT64_MIN;
    }
    uint32_t r = 0;
    while (valid && r < emitted->code.count && emitted->origins[r] == prologue)
    {
        const X86Instruction record = emitted->code.records[r];
        if (record.form == X86_FORM_PUSH_R64 && record.rm < 16 && record.rm != X86_REGISTER_RSP && build->saves[record.rm] == INT64_MIN &&
            tier1_cert_emitted_exactly(record, (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = record.rm }))
        {
            sp -= 8;
            build->saves[record.rm] = sp;
        }
        else if (build->frame_pointer == INT64_MIN && tier1_cert_emitted_exactly(record, (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RSP, .rm = X86_REGISTER_RBP }))
        {
            build->frame_pointer = sp;
        }
        else if (tier1_cert_emitted_immediate_form(record.form, X86_FORM_SUB_RM64_IMM8, X86_FORM_SUB_RM64_IMM32, record.immediate) && record.immediate > 0 &&
                 tier1_cert_emitted_exactly(record, (X86Instruction){ .form = record.form, .rm = X86_REGISTER_RSP, .immediate = record.immediate }))
        {
            sp -= record.immediate;
        }
        else
        {
            valid = tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r);
        }
        r += 1;
    }
    build->stack = sp;
    // The frame base register is rsp after the prologue, or the frame pointer it set.
    build->frame_base = build->base == X86_REGISTER_RSP ? sp : build->base == X86_REGISTER_RBP ? build->frame_pointer : INT64_MIN;
    valid = valid && (build->frame_base != INT64_MIN || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r));
    // The frame the layout's offsets are of: the pushes exactly its saved registers (and rbp with its frame pointer), the
    // frame pointer set exactly where it has one, its allocation exactly. And the ABI's alignment: rsp at entry is 8 past
    // a multiple of 16, and at a call a multiple of 16, the body's rsp the prologue's (a dynamic frame is left
    // uncertified).
    const Tier1Frame* frame = &emitted->frame;
    uint32_t pushed = 0;
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        pushed |= build->saves[reg] != INT64_MIN ? 1u << reg : 0;
    }
    const uint32_t expected = frame->saved_registers | (frame->frame_pointer ? 1u << X86_REGISTER_RBP : 0);
    valid = valid && ((pushed == expected && (build->frame_pointer != INT64_MIN) == frame->frame_pointer && sp == -8 * (int64_t)__builtin_popcount(expected) - (int64_t)frame->stack_adjust_bytes) ||
                      tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r));
    const bool calls = emitted->prepared.call_count != 0;
    valid = valid && (!calls || ((sp % 16) + 16) % 16 == 8 || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r));
    *end = r;
    return valid;
}

// A prologue's record `r` in the stream: a push's store of its register into its slot, the frame pointer's set killing
// rbp's value at entry, an allocation moving rsp alone (the stream's decoding follows rsp in Tier1CertEmittedBuild.sp).
static bool tier1_cert_emitted_prologue_record(Tier1CertEmittedBuild* build, uint32_t r)
{
    const X86Instruction record = build->emitted->code.records[r];
    bool valid = build->code_count < build->code_capacity && tier1_cert_emitted_take_reference(build, r, 0);
    if (valid && record.form == X86_FORM_PUSH_R64)
    {
        build->sp -= 8;
        const uint32_t slot = tier1_cert_emitted_frame(build, build->sp - build->frame_base, 8);
        valid = slot != BUSTER_CERT_NONE;
        build->code[build->code_count] = (Tier1CertInstruction){
            .a = slot,
            .b = tier1_cert_emitted_gpr(record.rm, 8),
            .op = TIER1_CERT_MOVE,
        };
        build->code_count += valid;
    }
    else if (valid && record.form == X86_FORM_MOV_RM64_R64)
    {
        Tier1CertSource* source = 0;
        Tier1CertInstruction* instruction = 0;
        valid = tier1_cert_emitted_original(build, build->plan->function->instruction_count, 5, r, &source, &instruction);
        if (valid)
        {
            source->after[source->after_count++] = tier1_cert_emitted_gpr(X86_REGISTER_RBP, 8);
        }
    }
    else if (valid)
    {
        build->sp -= record.immediate;
    }
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_CAPACITY, r);
}

// An epilogue's records [first, end) in the stream, from rsp after the prologue (the body moves it only by a call's own
// balanced push and pop): each pop's load of its register from the slot at rsp, the release's add, the reset of rsp from
// the frame pointer. Nothing else may be in it.
static bool tier1_cert_emitted_epilogue(Tier1CertEmittedBuild* build, uint32_t first, uint32_t end)
{
    const Tier1EmitResult* emitted = build->emitted;
    int64_t frame_pointer = build->frame_pointer;
    build->sp = build->stack;
    bool valid = true;
    for (uint32_t r = first; valid && r < end; r += 1)
    {
        const X86Instruction record = emitted->code.records[r];
        valid = tier1_cert_emitted_take_reference(build, r, 0);
        if (valid && tier1_cert_emitted_immediate_form(record.form, X86_FORM_ADD_RM64_IMM8, X86_FORM_ADD_RM64_IMM32, record.immediate) && record.immediate > 0 &&
            tier1_cert_emitted_exactly(record, (X86Instruction){ .form = record.form, .rm = X86_REGISTER_RSP, .immediate = record.immediate }))
        {
            build->sp += record.immediate;
        }
        else if (valid && frame_pointer != INT64_MIN &&
                 tier1_cert_emitted_exactly(record, (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RSP, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = record.displacement }))
        {
            build->sp = frame_pointer + record.displacement;
        }
        else if (valid && frame_pointer != INT64_MIN && tier1_cert_emitted_exactly(record, (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RBP, .rm = X86_REGISTER_RSP }))
        {
            build->sp = frame_pointer;
        }
        else if (valid && record.form == X86_FORM_POP_R64 && record.rm < 16 && record.rm != X86_REGISTER_RSP && tier1_cert_emitted_exactly(record, (X86Instruction){ .form = X86_FORM_POP_R64, .rm = record.rm }))
        {
            const uint32_t slot = tier1_cert_emitted_frame(build, build->sp - build->frame_base, 8);
            valid = slot != BUSTER_CERT_NONE && build->code_count < build->code_capacity;
            if (valid)
            {
                build->code[build->code_count++] = (Tier1CertInstruction){
                    .a = tier1_cert_emitted_gpr(record.rm, 8),
                    .b = slot,
                    .op = TIER1_CERT_MOVE,
                };
            }
            build->sp += 8;
            frame_pointer = record.rm == X86_REGISTER_RBP ? INT64_MIN : frame_pointer;
        }
        else
        {
            valid = false;
        }
        valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r);
    }
    return valid;
}

// A return's ret: rsp back at the return address, and an instruction that reads every callee-saved register's value at
// entry in its register and the return address in its slot.
static bool tier1_cert_emitted_return(Tier1CertEmittedBuild* build, uint32_t node, const X86Instruction* ret, uint32_t r)
{
    Tier1CertEmittedKernel kernel = {
        .definition = BUSTER_TIER1_CFG_NONE,
        .definition_location = BUSTER_CERT_NONE,
    };
    bool valid = build->sp == 0 || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r);
    for (uint32_t k = 0; valid && k < BUSTER_ARRAY_LENGTH(tier1_cert_emitted_callee); k += 1)
    {
        valid = tier1_cert_emitted_add_use(&kernel, build->pseudo + k, tier1_cert_emitted_gpr(tier1_cert_emitted_callee[k], 8));
    }
    valid = valid && tier1_cert_emitted_add_use(&kernel, build->pseudo + 6, tier1_cert_emitted_frame(build, -build->frame_base, 8));
    return valid && tier1_cert_emitted_originals(build, node, &kernel, ret, 1);
}

// Node `node`'s run from record `r` before block tail `tail`: its records (their origin `origin`), a return's epilogue
// (which may have none) before its ret among them, as its kernel says; then its ORIGINALs. `next` is the record after
// the run.
static bool tier1_cert_emitted_run(Tier1CertEmittedBuild* build, uint32_t node, uint32_t origin, uint32_t r, uint32_t tail, uint16_t* condition, uint32_t* next)
{
    const Tier1EmitResult* emitted = build->emitted;
    const IrFunction* function = build->plan->function;
    const uint32_t epilogue_origin = tier1_origin(TIER1_ORIGIN_FRAME, 1);
    const bool returns = function->op[node] == IR_RET;
    Tier1CertEmittedKernel kernel = { 0 };
    bool valid = tier1_cert_emitted_kernel(build, node, &kernel) || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    X86Instruction found[BUSTER_CERT_EMITTED_KERNEL];
    uint32_t found_count = 0;
    uint32_t at = r;
    bool epilogue = false;
    // The epilogue's records, and the records before it.
    uint32_t epilogue_first = UINT32_MAX;
    uint32_t epilogue_end = UINT32_MAX;
    uint32_t before = 0;
    bool more = valid;
    while (more && at < tail && found_count < BUSTER_CERT_EMITTED_KERNEL)
    {
        const uint32_t here = emitted->origins[at];
        if (here == origin)
        {
            valid = tier1_cert_emitted_take_reference(build, at, found_count == kernel.referenced ? &kernel.reference : 0);
            found[found_count] = emitted->code.records[at];
            found_count += 1;
            at += 1;
            // After the epilogue, the ret ends the run.
            more = valid && !epilogue;
        }
        else if (here == epilogue_origin && returns && !epilogue && found_count + 1 == kernel.count)
        {
            epilogue = true;
            epilogue_first = at;
            before = found_count;
            while (at < tail && emitted->origins[at] == epilogue_origin)
            {
                at += 1;
            }
            epilogue_end = at;
        }
        else
        {
            more = false;
        }
    }
    valid = valid && found_count == kernel.count && memcmp(found, kernel.records, (uint64_t)found_count * sizeof(X86Instruction)) == 0;
    valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    if (returns)
    {
        // A return: its records before its ret, then its epilogue (none where the frame has none), then the ret, which
        // gives back the callee-saved registers and returns.
        before = epilogue ? before : found_count - (found_count != 0);
        valid = valid && found_count != 0 && tier1_cert_emitted_originals(build, node, &kernel, found, before);
        valid = valid && (!epilogue || tier1_cert_emitted_epilogue(build, epilogue_first, epilogue_end));
        build->sp = epilogue ? build->sp : build->stack;
        valid = valid && tier1_cert_emitted_return(build, node, &found[found_count - 1], at - 1);
    }
    else
    {
        valid = valid && tier1_cert_emitted_originals(build, node, &kernel, found, found_count);
    }
    *condition = function->op[node] == IR_CBR ? kernel.condition : *condition;
    *next = at;
    return valid;
}

// The block's nodes from `*cursor` up to `node`, which have no records before `record`: each one that may have none
// (tier1_cert_emitted_silent), or whose kernel has none, a copy that vanishes in its own register and is its source's
// value there. A conditional branch with two successors (`degree`) and no comparison of its own gives its condition.
static bool tier1_cert_emitted_flush(Tier1CertEmittedBuild* build, uint32_t* cursor, uint32_t node, uint32_t record, uint32_t degree, uint16_t* condition)
{
    const Tier1SelectPlan* plan = build->plan;
    const IrFunction* function = plan->function;
    bool valid = true;
    for (uint32_t n = *cursor; valid && n < node; n += 1)
    {
        bool silent = tier1_cert_emitted_silent(build->emitted, plan, n);
        const bool branch = function->op[n] == IR_CBR && !plan->covered[n] && degree == 2;
        Tier1CertEmittedKernel kernel = { 0 };
        if (!silent || branch)
        {
            valid = tier1_cert_emitted_kernel(build, n, &kernel) && kernel.count == 0;
        }
        else
        {
            // A recomputed definition the consumer says it left out (Tier1EmitResult.dropped); otherwise it is there with
            // no records, a copy that vanishes. Either claim is checked: a left-out value's uses need its recomputations,
            // and a vanished copy's operand must be in its register.
            const IrFunction* f = function;
            const bool selected = !plan->covered[n] && f->op[n] != IR_PHI && !tier1_select_entry_parameter(f, n) && f->op[n] != IR_BR && f->op[n] != IR_CBR && f->op[n] != IR_BR_INDIRECT;
            const bool kept = selected && build->emitted->dropped[n] == 0;
            valid = !kept || (tier1_cert_emitted_kernel(build, n, &kernel) && kernel.count == 0);
            silent = !kept;
        }
        if (valid && branch)
        {
            *condition = kernel.condition;
        }
        else if (valid && !silent && kernel.definition != BUSTER_TIER1_CFG_NONE && kernel.use_count == 1 && build->locations[kernel.use_locations[0]].unit == build->locations[kernel.definition_location].unit &&
                 build->locations[kernel.use_locations[0]].width == build->locations[kernel.definition_location].width)
        {
            // Its definition is its one operand's bits, in the same register: a copy, which the operand's value stays
            // in too. One of another width (a canonical 32-bit value's 64-bit zero extension) is an instruction like
            // any other, below.
            Tier1CertSource* source = 0;
            Tier1CertInstruction* instruction = 0;
            valid = tier1_cert_emitted_original(build, n, 3, 0, &source, &instruction);
            if (valid)
            {
                source->operands[0] = (Tier1CertOperand){
                    .value = kernel.uses[0],
                    .allowed_registers = UINT32_MAX,
                };
                source->operands[1] = (Tier1CertOperand){
                    .value = kernel.definition,
                    .allowed_registers = UINT32_MAX,
                };
                source->use_count = 1;
                source->def_count = 1;
                source->copy = true;
                instruction->locations[0] = kernel.use_locations[0];
                instruction->locations[1] = kernel.definition_location;
                instruction->operand_count = 2;
                build->stats->originals += 1;
            }
        }
        else if (valid && !silent)
        {
            valid = tier1_cert_emitted_originals(build, n, &kernel, 0, 0);
        }
        valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, record);
    }
    *cursor = node > *cursor ? node : *cursor;
    return valid;
}

// Record `r` of the entry block's body, its origin ENTRY's: a narrow parameter's extension into its canonical form, which
// is the parameter itself, or a parameter's move into its register.
static bool tier1_cert_emitted_entry(Tier1CertEmittedBuild* build, uint32_t r)
{
    const Tier1EmitResult* emitted = build->emitted;
    const IrFunction* function = build->plan->function;
    const X86Instruction record = emitted->code.records[r];
    const uint32_t node = emitted->origin_values[r];
    const bool parameter = node < function->instruction_count && function->op[node] == IR_PARAM;
    const uint32_t type = parameter ? function->type[node] : IR_VOID;
    const uint16_t extend = type == IR_I1 || type == IR_I8 ? X86_FORM_MOVZX_R32_RM8 : type == IR_I16 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_NONE;
    const Tier1SelectPlace place = parameter ? tier1_select_parameter_place(function, function->a[node]) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_STACK };
    const bool narrowed = extend != X86_FORM_NONE && record.form == extend && place.kind == BUSTER_TIER1_PLACE_GENERAL && record.reg == tier1_select_parameter_registers[place.index] && record.rm == record.reg &&
                          record.mode == BUSTER_X86_REGISTER_DIRECT;
    // A stack parameter's load from its incoming slot, by its type's form: a narrow one's zero extension into its canonical
    // form, an I32's 32-bit load, a 64-bit one's, a floating one's into an xmm register, or its bits into a general one.
    uint8_t base = 0;
    int32_t offset = 0;
    const bool incoming = place.kind == BUSTER_TIER1_PLACE_STACK && tier1_emit_cfg_incoming(emitted, (uint64_t)BUSTER_ABI_SLOT_BYTES * place.index, &base, &offset) && tier1_cert_emitted_frame_operand(build, record) && record.displacement == offset;
    const uint32_t float_width = parameter ? tier1_select_float_width(type) : 0;
    const uint16_t general_form = type == IR_I64 || type == IR_PTR || float_width == 8 ? X86_FORM_MOV_R64_RM64 : type == IR_I32 || float_width == 4 ? X86_FORM_MOV_R32_RM32 : extend;
    const uint16_t vector_form = float_width != 0 ? tier1_select_float_forms(float_width)->load : X86_FORM_NONE;
    bool valid = !incoming || record.form == general_form || record.form == vector_form;
    if (valid && incoming && extend != X86_FORM_NONE && record.reg < 16)
    {
        // Its canonical 4 bytes from its slot's.
        const uint32_t from = tier1_cert_emitted_frame(build, offset, 4);
        valid = from != BUSTER_CERT_NONE && build->code_count < build->code_capacity && tier1_cert_emitted_take_reference(build, r, 0);
        if (valid)
        {
            build->code[build->code_count++] = (Tier1CertInstruction){
                .a = tier1_cert_emitted_gpr(record.reg, 4),
                .b = from,
                .op = TIER1_CERT_MOVE,
            };
            build->stats->edits += 1;
        }
    }
    else if (valid && narrowed)
    {
        valid = tier1_cert_emitted_take_reference(build, r, 0);
    }
    else if (valid)
    {
        valid = build->code_count < build->code_capacity && tier1_cert_emitted_move(build, r, BUSTER_IR_NONE, build->code + build->code_count);
        build->code_count += valid;
        build->stats->edits += valid;
    }
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_DECODE, r);
}

static Tier1CertEmittedReason tier1_certificate_emitted(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, Arena* output, Arena* scratch, Tier1CertProgram* program, Tier1CertEmittedStats* stats)
{
    const uint64_t output_saved = output->length;
    const uint64_t scratch_saved = scratch->length;
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const Tier1SelectCode* code = &emitted->code;
    const uint32_t count = function->instruction_count;
    const uint32_t blocks = input->block_count;
    const uint32_t edges = input->edge_count;
    const uint32_t values = input->value_count;
    const uint32_t records = code->count;
    const uint32_t added = blocks - function->block_count;
    Tier1CertEmittedStats tally = {
        .record = UINT32_MAX,
        .records = records,
    };
    Tier1CertEmittedBuild build = {
        .plan = plan,
        .emitted = emitted,
        .stats = &tally,
        .base = (uint8_t)emitted->frame.base_register,
        .base_position = 2 * added,
    };
    tally.reason = emitted->origins != 0 && emitted->block_records != 0 && emitted->stub_records != 0 && emitted->dropped != 0 ? tier1_cert_emitted_supported(plan, emitted) : TIER1_CERT_EMITTED_ORIGIN;
    bool valid = tally.reason == TIER1_CERT_EMITTED_OK;
    // The blocks' records in layout order, every record of one.
    for (uint32_t b = 0; valid && b < blocks; b += 1)
    {
        valid = emitted->block_records[b] <= emitted->block_records[b + 1] && (b != 0 || emitted->block_records[0] == 0);
    }
    valid = valid && emitted->block_records[blocks] == records;
    valid = valid || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, UINT32_MAX);
    // The prologue's frame: rsp after it, the frame base register's offset, the pushes' slots.
    uint32_t prologue_end = 0;
    valid = valid && tier1_cert_emitted_prologue(&build, &prologue_end);
    build.pseudo = values;
    // The frame's storage rows, the spills', the saves' and the temporary slots', each 16 bytes at most: the frame base
    // register's displacements the stream's moves may name.
    const Tier1EmitPrepared* prepared = &emitted->prepared;
    const uint32_t rows = cfg->allocation.spill_count + (uint32_t)__builtin_popcount(prepared->call_save_mask) + (uint32_t)__builtin_popcount(prepared->mask_save_mask) + (uint32_t)__builtin_popcount(prepared->vector_save_mask) +
                          (uint32_t)__builtin_popcount(cfg->temp_slots & ((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1));
    int64_t low = INT32_MAX;
    int64_t high = INT32_MIN;
    for (uint32_t row = 0; valid && row < rows && row < emitted->frame.count; row += 1)
    {
        low = emitted->frame.offset[row] < low ? emitted->frame.offset[row] : low;
        high = (int64_t)emitted->frame.offset[row] + 16 > high ? (int64_t)emitted->frame.offset[row] + 16 : high;
    }
    // And the calls' outgoing areas from rsp, and the stack parameters' incoming slots: the frame base register's too
    // (tier1_cert_emitted_supported).
    for (uint32_t node = 0; valid && node < count; node += 1)
    {
        const uint32_t op = function->op[node];
        const uint32_t outgoing = (op == IR_CALL || op == IR_CALL_INDIRECT) && !plan->covered[node] ? tier1_select_call_stack_bytes(function, &function->calls[function->b[node]]) : 0;
        const Tier1SelectPlace place = tier1_select_entry_parameter(function, node) ? tier1_select_parameter_place(function, function->a[node]) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_GENERAL };
        uint8_t base = 0;
        int32_t offset = 0;
        if (outgoing != 0)
        {
            low = low < 0 ? low : 0;
            high = (int64_t)outgoing > high ? (int64_t)outgoing : high;
        }
        if (place.kind == BUSTER_TIER1_PLACE_STACK && tier1_emit_cfg_incoming(emitted, (uint64_t)BUSTER_ABI_SLOT_BYTES * place.index, &base, &offset))
        {
            low = offset < low ? offset : low;
            high = (int64_t)offset + BUSTER_ABI_SLOT_BYTES > high ? (int64_t)offset + BUSTER_ABI_SLOT_BYTES : high;
        }
    }
    // And the pushes' slots and the return address's, by their offsets from the frame base register.
    for (uint32_t reg = 0; valid && reg < 16; reg += 1)
    {
        const int64_t displacement = build.saves[reg] != INT64_MIN ? build.saves[reg] - build.frame_base : INT64_MAX;
        low = displacement != INT64_MAX && displacement < low ? displacement : low;
        high = displacement != INT64_MAX && displacement + 8 > high ? displacement + 8 : high;
    }
    if (valid)
    {
        low = -build.frame_base < low ? -build.frame_base : low;
        high = 8 - build.frame_base > high ? 8 - build.frame_base : high;
    }
    valid = valid && (low >= INT32_MIN && high <= INT32_MAX);
    build.low = low <= high ? (int32_t)low : 0;
    build.high = low <= high && high <= INT32_MAX ? (int32_t)high : 0;
    valid = valid && ((uint64_t)((int64_t)build.high - build.low) <= BUSTER_CERT_MAX_TOTAL - BUSTER_CERT_EMITTED_FRAME_UNIT || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
    // The facts: a block's for each interval that carries its value into it.
    uint64_t fact_capacity = 0;
    for (uint32_t i = 0; valid && i < cfg->intervals.count; i += 1)
    {
        for (uint32_t b = tier1_cfg_block_of(input, cfg->intervals.starts[i]); b < blocks && input->block_starts[b] < cfg->intervals.ends[i]; b += 1)
        {
            fact_capacity += 1;
        }
    }
    // The arrays: the locations (the registers', then a frame range per record and per kernel's store at most), the
    // values, the facts, the source and code, the blocks, edges and renames; and the scratch maps.
    build.location_capacity = BUSTER_CERT_EMITTED_REGISTERS + 4 * records + 8 * blocks + 64;
    build.source_capacity = BUSTER_CERT_EMITTED_NODE_SOURCES * count + 16;
    build.code_capacity = records + build.source_capacity;
    build.locations = valid ? arena_push_array(output, build.location_capacity, sizeof(Tier1CertLocation), _Alignof(Tier1CertLocation)) : 0;
    Tier1CertValue* cert_values = valid ? arena_push_array(output, (uint64_t)values + BUSTER_CERT_EMITTED_PSEUDO + 1, sizeof(Tier1CertValue), _Alignof(Tier1CertValue)) : 0;
    Tier1CertFact* facts = valid ? arena_push_array(output, fact_capacity + count + BUSTER_CERT_EMITTED_PSEUDO * ((uint64_t)blocks + 1) + 1, sizeof(Tier1CertFact), _Alignof(Tier1CertFact)) : 0;
    build.source = valid ? arena_push_array(output, build.source_capacity, sizeof(Tier1CertSource), _Alignof(Tier1CertSource)) : 0;
    build.code = valid ? arena_push_array(output, build.code_capacity, sizeof(Tier1CertInstruction), _Alignof(Tier1CertInstruction)) : 0;
    Tier1CertBlock* cert_blocks = valid ? arena_push_array(output, (uint64_t)blocks + 1, sizeof(Tier1CertBlock), _Alignof(Tier1CertBlock)) : 0;
    Tier1CertEdge* cert_edges = valid ? arena_push_array(output, (uint64_t)edges + 1, sizeof(Tier1CertEdge), _Alignof(Tier1CertEdge)) : 0;
    Tier1CertRename* renames = valid ? arena_push_array(output, (uint64_t)input->phi_operand_offsets[input->phi_count] + 1, sizeof(Tier1CertRename), _Alignof(Tier1CertRename)) : 0;
    build.references = valid ? arena_push_array(scratch, (uint64_t)records + 1, 4, 4) : 0;
    build.branch_of = valid ? arena_push_array(scratch, (uint64_t)records + 1, 4, 4) : 0;
    build.claims = valid ? arena_push(scratch, (uint64_t)count + 1, 1) : 0;
    uint32_t* numbers = valid ? arena_push_array(scratch, (uint64_t)blocks + 1, 4, 4) : 0;
    uint32_t* queue = valid ? arena_push_array(scratch, (uint64_t)blocks + 1, 4, 4) : 0;
    uint32_t* fact_offsets = valid ? arena_push_array(scratch, (uint64_t)blocks + 2, 4, 4) : 0;
    uint32_t* fill = valid ? arena_push_array(scratch, (uint64_t)blocks + 1, 4, 4) : 0;
    uint8_t* is_phi = valid ? arena_push(scratch, (uint64_t)values + 1, 1) : 0;
    uint32_t* edge_offsets = valid ? arena_push_array(scratch, (uint64_t)edges + 2, 4, 4) : 0;
    uint32_t* edge_list = valid ? arena_push_array(scratch, (uint64_t)records + 1, 4, 4) : 0;
    uint32_t* edge_index = valid ? arena_push_array(scratch, (uint64_t)edges + 1, 4, 4) : 0;
    const bool arrays = build.locations != 0 && cert_values != 0 && facts != 0 && build.source != 0 && build.code != 0 && cert_blocks != 0 && cert_edges != 0 && renames != 0 && build.references != 0 && build.branch_of != 0 &&
                        build.claims != 0 && numbers != 0 && queue != 0 && fact_offsets != 0 && fill != 0 && is_phi != 0 && edge_offsets != 0 && edge_list != 0 && edge_index != 0;
    valid = valid && (arrays || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
    if (valid)
    {
        static const uint16_t xmm_widths[3] = { 16, 8, 4 };
        for (uint32_t r = 0; r < BUSTER_CERT_EMITTED_REGISTERS; r += 1)
        {
            const bool gpr = r < BUSTER_CERT_EMITTED_XMM;
            const bool xmm = r >= BUSTER_CERT_EMITTED_XMM && r < BUSTER_CERT_EMITTED_MASK;
            const uint32_t index = gpr ? r / 2 : xmm ? (r - BUSTER_CERT_EMITTED_XMM) / 3 : (r - BUSTER_CERT_EMITTED_MASK) / 4;
            build.locations[r] = (Tier1CertLocation){
                .unit = gpr ? 8 * index : xmm ? 128 + 16 * index : 384 + 8 * index,
                .width = gpr ? (r % 2 == 0 ? 8 : 4) : xmm ? xmm_widths[(r - BUSTER_CERT_EMITTED_XMM) % 3] : (uint16_t)(1u << ((r - BUSTER_CERT_EMITTED_MASK) % 4)),
                .extent = xmm ? 16 : 8,
                .class = gpr ? TIER1_CERT_GPR : xmm ? TIER1_CERT_VECTOR : TIER1_CERT_MASK,
                .reg = (uint8_t)index,
            };
        }
        build.location_count = BUSTER_CERT_EMITTED_REGISTERS;
        for (uint32_t v = 0; v < values; v += 1)
        {
            const uint32_t class = input->classes[v];
            const bool recipe = input->rematerializable != 0 && input->rematerializable[v] != 0 && class == BUSTER_TIER1_ALLOCATE_GPR;
            cert_values[v] = (Tier1CertValue){
                .payload = recipe ? v : 0,
                .width = tier1_cert_emitted_width(emitted, v),
                .class = class == BUSTER_TIER1_ALLOCATE_GPR ? TIER1_CERT_GPR : class == BUSTER_TIER1_ALLOCATE_VECTOR ? TIER1_CERT_VECTOR : TIER1_CERT_MASK,
                .recipe = recipe ? TIER1_CERT_INTEGER_RECIPE : TIER1_CERT_NO_RECIPE,
            };
        }
        // The callee-saved registers' values at entry and the return address.
        for (uint32_t k = 0; k < BUSTER_CERT_EMITTED_PSEUDO; k += 1)
        {
            cert_values[values + k] = (Tier1CertValue){
                .width = 8,
                .class = TIER1_CERT_GPR,
            };
        }
        memset(build.references, 0xff, ((uint64_t)records + 1) * 4);
        memset(build.branch_of, 0xff, ((uint64_t)records + 1) * 4);
        memset(build.claims, UINT8_MAX, (uint64_t)count + 1);
        memset(is_phi, 0, (uint64_t)values + 1);
        memset(edge_offsets, 0, ((uint64_t)edges + 2) * 4);
        for (uint32_t k = 0; k < input->phi_count; k += 1)
        {
            is_phi[input->phi_values[k]] = 1;
        }
    }
    // Each record's reference, one at most.
    for (uint32_t k = 0; valid && k < emitted->reference_count; k += 1)
    {
        const uint32_t r = emitted->references[k].instruction;
        valid = (r < records && build.references[r] == UINT32_MAX) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_REFERENCE, r);
        build.references[valid ? r : records] = k;
    }
    // Each record's origin, of a kind and payload that can be; each edge's records, in order.
    for (uint32_t r = 0; valid && r < records; r += 1)
    {
        const uint32_t origin = emitted->origins[r];
        const Tier1Origin kind = tier1_cert_emitted_kind(origin);
        const uint32_t payload = origin & BUSTER_TIER1_ORIGIN_PAYLOAD;
        valid = kind > TIER1_ORIGIN_NONE && kind < TIER1_ORIGIN_OTHER && (kind != TIER1_ORIGIN_EDGE || payload < edges) && (kind != TIER1_ORIGIN_NODE || payload < count) && (kind != TIER1_ORIGIN_FRAME || payload <= 1);
        if (valid && kind == TIER1_ORIGIN_EDGE)
        {
            edge_offsets[payload + 2] += 1;
        }
        valid = valid || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, r);
    }
    for (uint32_t e = 0; valid && e < edges; e += 1)
    {
        edge_offsets[e + 2] += edge_offsets[e + 1];
    }
    for (uint32_t r = 0; valid && r < records; r += 1)
    {
        const uint32_t origin = emitted->origins[r];
        if (tier1_cert_emitted_kind(origin) == TIER1_ORIGIN_EDGE)
        {
            edge_list[edge_offsets[(origin & BUSTER_TIER1_ORIGIN_PAYLOAD) + 1]++] = r;
        }
    }
    // A branch is a jump's or, within it, the prologue's (its probes' loop).
    for (uint32_t k = 0; valid && k < code->branch_count; k += 1)
    {
        const uint32_t r = code->branches[k];
        const uint32_t t = code->targets[k];
        valid = r < records && t <= records && build.branch_of[r] == UINT32_MAX;
        const Tier1Origin kind = valid ? tier1_cert_emitted_kind(emitted->origins[r]) : TIER1_ORIGIN_NONE;
        valid = valid && (kind == TIER1_ORIGIN_CONTROL || (kind == TIER1_ORIGIN_FRAME && t < records && emitted->origins[t] == emitted->origins[r]));
        valid = valid || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CONTROL, r);
        build.branch_of[valid ? r : records] = k;
    }
    // Reachable blocks, numbered in layout order; each edge's index among its successor's predecessors.
    uint32_t reachable = 0;
    if (valid)
    {
        memset(numbers, 0xff, ((uint64_t)blocks + 1) * 4);
        numbers[0] = 0;
        queue[0] = 0;
        uint32_t tail = 1;
        for (uint32_t head = 0; head < tail; head += 1)
        {
            const uint32_t b = queue[head];
            for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
            {
                const uint32_t s = input->successors[e];
                if (numbers[s] == BUSTER_CERT_NONE)
                {
                    numbers[s] = 0;
                    queue[tail++] = s;
                }
            }
        }
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            numbers[b] = numbers[b] != BUSTER_CERT_NONE ? reachable++ : BUSTER_CERT_NONE;
            for (uint32_t e = input->successor_offsets[b]; e < input->successor_offsets[b + 1]; e += 1)
            {
                const uint32_t s = input->successors[e];
                edge_index[e] = BUSTER_CERT_NONE;
                for (uint32_t j = input->predecessor_offsets[s + 1]; j > input->predecessor_offsets[s]; j -= 1)
                {
                    edge_index[e] = input->predecessors[j - 1] == b ? j - 1 - input->predecessor_offsets[s] : edge_index[e];
                }
            }
        }
    }
    // The entry's facts: each parameter in its ABI register, which the prologue doesn't write.
    uint32_t entry_count = 0;
    uint64_t entry_registers = 0;
    for (uint32_t node = 0; valid && function->block_count != 0 && node < function->block_offsets[1]; node += 1)
    {
        const uint32_t v = emitted->node_values[node];
        if (tier1_select_entry_parameter(function, node) && v != BUSTER_TIER1_CFG_NONE)
        {
            const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
            const bool general = place.kind == BUSTER_TIER1_PLACE_GENERAL;
            const bool stacked = place.kind == BUSTER_TIER1_PLACE_STACK;
            const uint32_t reg = general ? tier1_select_parameter_registers[place.index] : place.index;
            uint8_t base = 0;
            int32_t offset = 0;
            // A stack parameter is its incoming slot's bytes.
            const bool incoming = stacked && tier1_emit_cfg_incoming(emitted, (uint64_t)BUSTER_ABI_SLOT_BYTES * place.index, &base, &offset);
            facts[entry_count] = (Tier1CertFact){
                .value = v,
                .location = stacked ? (incoming ? tier1_cert_emitted_frame(&build, offset, tier1_cert_emitted_width(emitted, v)) : BUSTER_CERT_NONE)
                                    : tier1_cert_emitted_register(emitted, v, 32 * (general ? BUSTER_TIER1_ALLOCATE_GPR : BUSTER_TIER1_ALLOCATE_VECTOR) + reg),
            };
            valid = facts[entry_count].location != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_DECODE, UINT32_MAX);
            entry_registers |= stacked ? 0 : general ? BUSTER_X86_RESOURCE_GPR(reg) : BUSTER_X86_RESOURCE_VECTOR(reg);
            entry_count += 1;
        }
    }
    // The callee-saved registers' values at entry in their registers, and the return address at rsp.
    for (uint32_t k = 0; valid && k < BUSTER_CERT_EMITTED_PSEUDO; k += 1)
    {
        facts[entry_count] = (Tier1CertFact){
            .value = values + k,
            .location = k < 6 ? tier1_cert_emitted_gpr(tier1_cert_emitted_callee[k], 8) : tier1_cert_emitted_frame(&build, -build.frame_base, 8),
        };
        valid = facts[entry_count].location != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX);
        entry_count += 1;
    }
    // Every other reachable block's facts: the values its start's intervals carry in, where the allocation has them there
    // (a phi of the block's among them, not a value its start defines), counted then filled; then the protocol's values,
    // each callee-saved register's in its push's slot, or in itself where the prologue doesn't push it, and the return
    // address.
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        if (pass == 0)
        {
            memset(fact_offsets, 0, ((uint64_t)blocks + 2) * 4);
        }
        else
        {
            fact_offsets[0] = entry_count;
            for (uint32_t b = 0; b < blocks; b += 1)
            {
                fact_offsets[b + 1] += fact_offsets[b] + (b != 0 && numbers[b] != BUSTER_CERT_NONE ? BUSTER_CERT_EMITTED_PSEUDO : 0);
                fill[b] = fact_offsets[b];
            }
            for (uint32_t b = 1; valid && b < blocks; b += 1)
            {
                for (uint32_t k = 0; numbers[b] != BUSTER_CERT_NONE && k < BUSTER_CERT_EMITTED_PSEUDO; k += 1)
                {
                    const uint32_t reg = k < 6 ? tier1_cert_emitted_callee[k] : 0;
                    const int64_t at = k == 6 ? 0 : build.saves[reg];
                    facts[fill[b]] = (Tier1CertFact){
                        .value = values + k,
                        .location = k < 6 && at == INT64_MIN ? tier1_cert_emitted_gpr(reg, 8) : tier1_cert_emitted_frame(&build, at - build.frame_base, 8),
                    };
                    valid = facts[fill[b]].location != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX);
                    fill[b] += 1;
                }
            }
        }
        for (uint32_t i = 0; valid && i < cfg->intervals.count; i += 1)
        {
            const uint32_t v = cfg->intervals.values[i];
            const uint32_t start = cfg->intervals.starts[i];
            for (uint32_t b = tier1_cfg_block_of(input, start); valid && b < blocks && input->block_starts[b] < cfg->intervals.ends[i]; b += 1)
            {
                const uint32_t entry = input->block_starts[b];
                if (b != 0 && entry >= start && (input->definitions[v] != entry || is_phi[v] != 0) && numbers[b] != BUSTER_CERT_NONE)
                {
                    if (pass == 0)
                    {
                        fact_offsets[b + 1] += 1;
                    }
                    else
                    {
                        uint32_t where = 0;
                        uint8_t kind = 0;
                        int32_t offset = 0;
                        uint32_t location = BUSTER_CERT_NONE;
                        valid = tier1_cfg_entry_location(cfg, v, entry, &where, &kind);
                        if (valid && kind == BUSTER_TIER1_ALLOCATE_REGISTER)
                        {
                            location = tier1_cert_emitted_register(emitted, v, where);
                            valid = location != BUSTER_CERT_NONE;
                        }
                        else if (valid && kind == BUSTER_TIER1_ALLOCATE_SPILL)
                        {
                            location = tier1_emit_cfg_slot(emitted, where, &offset) ? tier1_cert_emitted_frame(&build, offset, tier1_cert_emitted_width(emitted, v)) : BUSTER_CERT_NONE;
                            valid = location != BUSTER_CERT_NONE;
                        }
                        facts[fill[b]++] = (Tier1CertFact){
                            .value = v,
                            .location = location,
                        };
                        valid = valid || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_DECODE, UINT32_MAX);
                    }
                }
            }
        }
    }
    tally.facts = valid ? fact_offsets[blocks] : 0;
    // The streams, block by block in layout order.
    uint32_t edge_count = 0;
    uint32_t rename_count = 0;
    for (uint32_t b = 0; valid && b < blocks; b += 1)
    {
        const bool live = numbers[b] != BUSTER_CERT_NONE;
        const uint32_t first_source = build.source_count;
        const uint32_t first_code = build.code_count;
        const uint32_t first = emitted->block_records[b];
        const uint32_t end = emitted->block_records[b + 1];
        const uint32_t node_first = b < added ? 0 : function->block_offsets[b - added];
        const uint32_t node_end = b < added ? 0 : function->block_offsets[b - added + 1];
        const uint32_t in_degree = input->predecessor_offsets[b + 1] - input->predecessor_offsets[b];
        const uint32_t degree = input->successor_offsets[b + 1] - input->successor_offsets[b];
        uint32_t cursor = node_first;
        // The moves of the edge into the block that it alone reaches, placed at its start; then its body; then its tail,
        // its jumps and its edges' moves.
        uint32_t lead = first;
        while (lead < end && tier1_cert_emitted_kind(emitted->origins[lead]) == TIER1_ORIGIN_EDGE && input->successors[emitted->origins[lead] & BUSTER_TIER1_ORIGIN_PAYLOAD] == b && in_degree == 1)
        {
            lead += 1;
        }
        uint32_t tail = lead;
        for (uint32_t r = lead; r < end; r += 1)
        {
            const Tier1Origin kind = tier1_cert_emitted_kind(emitted->origins[r]);
            tail = kind != TIER1_ORIGIN_EDGE && kind != TIER1_ORIGIN_CONTROL ? r + 1 : tail;
        }
        bool body = false;
        uint16_t condition = X86_FORM_NONE;
        for (uint32_t r = lead; live && valid && r < tail; r += 1)
        {
            const uint32_t origin = emitted->origins[r];
            const Tier1Origin kind = tier1_cert_emitted_kind(origin);
            const uint32_t payload = origin & BUSTER_TIER1_ORIGIN_PAYLOAD;
            if (kind == TIER1_ORIGIN_FRAME && payload == 0)
            {
                // The prologue: first in the entry block (tier1_cert_emitted_prologue), writing no parameter's register.
                uint64_t read = 0;
                uint64_t written = 0;
                x86_instruction_effects_scalar(&code->records[r], &read, &written);
                valid = (b == 0 && !body && r < prologue_end && (written & entry_registers) == 0) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_FRAME, r);
                valid = valid && tier1_cert_emitted_prologue_record(&build, r);
            }
            else if (kind == TIER1_ORIGIN_ENTRY)
            {
                valid = (b == 0 && cursor == node_first) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, r);
                valid = valid && tier1_cert_emitted_entry(&build, r);
                body = true;
            }
            else if (kind == TIER1_ORIGIN_MOVE)
            {
                // A move at a node's position, after its block's earlier nodes: or a save's restore after its node's run.
                const uint32_t node = payload >= build.base_position ? (payload - build.base_position) / 2 : UINT32_MAX;
                valid = (node + 1 >= cursor && node < node_end) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, r);
                valid = valid && tier1_cert_emitted_flush(&build, &cursor, node, r, degree, &condition);
                valid = valid && build.code_count < build.code_capacity && tier1_cert_emitted_move(&build, r, emitted->origin_values[r], build.code + build.code_count);
                build.code_count += valid;
                tally.edits += valid;
                body = true;
            }
            else if (kind == TIER1_ORIGIN_NODE)
            {
                // A node's run, after its block's earlier nodes.
                valid = (payload >= cursor && payload < node_end) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_KERNEL, r);
                valid = valid && tier1_cert_emitted_flush(&build, &cursor, payload, r, degree, &condition);
                uint32_t next = r + 1;
                valid = valid && tier1_cert_emitted_run(&build, payload, origin, r, tail, &condition, &next);
                cursor = payload + 1;
                r = next - 1;
                body = true;
            }
            else if (kind == TIER1_ORIGIN_FRAME)
            {
                // An epilogue: its return's run, whose ret alone follows it.
                uint32_t at = r;
                while (at < tail && emitted->origins[at] == origin)
                {
                    at += 1;
                }
                const uint32_t returning = at < tail ? emitted->origins[at] : 0;
                const uint32_t node = returning & BUSTER_TIER1_ORIGIN_PAYLOAD;
                valid = (tier1_cert_emitted_kind(returning) == TIER1_ORIGIN_NODE && node >= cursor && node < node_end && function->op[node] == IR_RET) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, r);
                valid = valid && tier1_cert_emitted_flush(&build, &cursor, node, r, degree, &condition);
                uint32_t next = r + 1;
                valid = valid && tier1_cert_emitted_run(&build, node, returning, r, tail, &condition, &next);
                cursor = node + 1;
                r = next - 1;
                body = true;
            }
            else
            {
                valid = tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, r);
            }
        }
        // The block's nodes past its last record.
        valid = valid && (!live || tier1_cert_emitted_flush(&build, &cursor, node_end, tail, degree, &condition));
        Tier1CertBlock* block = live ? cert_blocks + numbers[b] : 0;
        if (live && valid)
        {
            *block = (Tier1CertBlock){
                .source = build.source + first_source,
                .code = build.code + first_code,
                .certificate = facts + (b == 0 ? 0 : fact_offsets[b]),
                .edges = cert_edges + edge_count,
                .source_count = build.source_count - first_source,
                .code_count = build.code_count - first_code,
                .certificate_count = b == 0 ? entry_count : fact_offsets[b + 1] - fact_offsets[b],
            };
        }
        // Its edges: each walked from its tail; its moves decoded in record order; its successor's phis renamed.
        valid = valid && (!live || degree != 0 || tail == end || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CONTROL, tail));
        valid = valid && (!live || degree != 2 || condition != X86_FORM_NONE || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CONTROL, tail));
        for (uint32_t e = input->successor_offsets[b]; live && valid && e < input->successor_offsets[b + 1]; e += 1)
        {
            const uint32_t s = input->successors[e];
            const uint32_t s_first = emitted->block_records[s];
            const uint32_t moves = edge_offsets[e + 1] - edge_offsets[e];
            uint32_t taken = 0;
            valid = tier1_cert_emitted_walk(&build, b, e, e == input->successor_offsets[b], degree == 2 ? condition : X86_FORM_NONE, tail, &taken);
            // The edge's moves: all on the path, or all at its successor's start, which this edge alone reaches.
            uint32_t at_start = 0;
            while (s_first + at_start < records && emitted->origins[s_first + at_start] == tier1_origin(TIER1_ORIGIN_EDGE, e))
            {
                at_start += 1;
            }
            const bool on_path = taken == moves;
            const bool at_successor = taken == 0 && at_start == moves && input->predecessor_offsets[s + 1] - input->predecessor_offsets[s] == 1;
            valid = valid && (on_path || at_successor || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CONTROL, s_first));
            Tier1CertEdge* edge = cert_edges + edge_count;
            *edge = (Tier1CertEdge){
                .renames = renames + rename_count,
                .moves = build.code + build.code_count,
                .destination = numbers[s],
            };
            for (uint32_t k = input->phi_offsets[s]; valid && k < input->phi_offsets[s + 1]; k += 1)
            {
                const uint32_t j = edge_index[e];
                valid = j != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CONTROL, UINT32_MAX);
                const uint32_t operand = valid ? input->phi_operands[input->phi_operand_offsets[k] + j] : 0;
                renames[rename_count] = (Tier1CertRename){
                    .destination = input->phi_values[k],
                    .source = operand == BUSTER_TIER1_CFG_NONE ? BUSTER_CERT_NONE : operand,
                };
                rename_count += valid;
            }
            for (uint32_t k = edge_offsets[e]; valid && k < edge_offsets[e + 1]; k += 1)
            {
                const uint32_t r = edge_list[k];
                valid = build.code_count < build.code_capacity && tier1_cert_emitted_move(&build, r, emitted->origin_values[r], build.code + build.code_count);
                build.code_count += valid;
            }
            if (valid)
            {
                edge->rename_count = (uint32_t)(renames + rename_count - edge->renames);
                edge->move_count = moves;
                tally.edge_edits += moves;
                edge_count += 1;
                block->edge_count += 1;
            }
        }
    }
    valid = valid && (build.location_count <= BUSTER_CERT_MAX_RECORDS || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
    if (valid)
    {
        const uint32_t units = BUSTER_CERT_EMITTED_FRAME_UNIT + (uint32_t)((int64_t)build.high - build.low);
        *program = (Tier1CertProgram){
            .locations = build.locations,
            .values = cert_values,
            .blocks = cert_blocks,
            .entry = facts,
            .unit_count = units,
            .location_count = build.location_count,
            .value_count = values + BUSTER_CERT_EMITTED_PSEUDO,
            .block_count = reachable,
            .entry_count = entry_count,
            .flags_location = BUSTER_CERT_NONE,
            .features = BUSTER_CERT_AVX512F | BUSTER_CERT_AVX512DQ | BUSTER_CERT_AVX512BW | BUSTER_CERT_AVX2,
        };
        tally.locations = build.location_count;
        tally.units = units;
        tally.bytes = (uint64_t)build.location_count * sizeof(Tier1CertLocation) + ((uint64_t)values + BUSTER_CERT_EMITTED_PSEUDO) * sizeof(Tier1CertValue) + tally.facts * sizeof(Tier1CertFact) + (uint64_t)build.source_count * sizeof(Tier1CertSource) +
                      (uint64_t)build.code_count * sizeof(Tier1CertInstruction) + (uint64_t)reachable * sizeof(Tier1CertBlock) + (uint64_t)edge_count * sizeof(Tier1CertEdge) + (uint64_t)rename_count * sizeof(Tier1CertRename);
    }
    else
    {
        arena_pop_to(output, output_saved);
    }
    arena_pop_to(scratch, scratch_saved);
    if (stats != 0)
    {
        *stats = tally;
    }
    return tally.reason;
}

// Publishes the certificate of the emitted code and checks it: OK, an unsupported shape's reason (not certified, not
// refused), or a refusal: the decoding's, or TIER1_CERT_EMITTED_CHECK with the checker's error in `report`. Both arenas
// are restored.
static Tier1CertEmittedReason tier1_certificate_emitted_check(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, Arena* output, Arena* scratch, Tier1CertReport* report, Tier1CertEmittedStats* stats)
{
    const uint64_t output_saved = output->length;
    const uint64_t scratch_saved = scratch->length;
    Tier1CertProgram program = { 0 };
    Tier1CertReport checked = {
        .error = TIER1_CERT_OK,
        .block = BUSTER_CERT_NONE,
        .edge = BUSTER_CERT_NONE,
        .instruction = BUSTER_CERT_NONE,
    };
    Tier1CertEmittedStats tally = { 0 };
    Tier1CertEmittedReason reason = tier1_certificate_emitted(plan, emitted, output, scratch, &program, &tally);
    if (reason == TIER1_CERT_EMITTED_OK)
    {
        const size_t size = tier1_certificate_scratch_size(&program);
        void* buffer = size != 0 ? arena_push(scratch, size, 64) : 0;
        checked.error = buffer != 0 ? tier1_certificate_check(&program, buffer, size, &checked) : TIER1_CERT_CAPACITY;
        reason = checked.error == TIER1_CERT_OK ? reason : checked.error == TIER1_CERT_CAPACITY ? TIER1_CERT_EMITTED_CAPACITY : TIER1_CERT_EMITTED_CHECK;
        tally.reason = reason;
    }
    arena_pop_to(scratch, scratch_saved);
    arena_pop_to(output, output_saved);
    if (report != 0)
    {
        *report = checked;
    }
    if (stats != 0)
    {
        *stats = tally;
    }
    return reason;
}
