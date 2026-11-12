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
//   registers it is given: tier1_emit_cfg_kernel, tier1_select_fixed, and a machine row's template, the front end's
//   records that tier0_operation_valid admits, by the roles they name), the ABI's argument registers and its records of
//   by-value aggregates, the rematerialization recipes, the frame's layout of the frame base register's offsets, the
//   CFG with its phis, and x86 semantics' effects of each record (which registers and memory it writes).
// - Untrusted: the allocation, every emitted record and the emitter's account of them: each record's origin
//   (Tier1Origin), each block's and stub's first record, the references, and each machine row's placement
//   (Tier1RowPlacement). An origin only says which decoding applies, and a placement only how to place a row's
//   template; every record is decoded by its own bytes.
// - A move's record (mov, movss, movsd, movaps, kmov, between registers and the frame base's displacements) is the edit
//   its bytes say, at the width its form has; a recomputation is a constant's edit only where tier1_rematerialize writes
//   exactly that record (and its reference) for the value its origin names. A node's records are one ORIGINAL: its
//   operands read where the allocation has them at its use phase, every register and frame byte its records write
//   killed, its definition where the allocation's interval starts. A call's sequence is checked against what the IR says
//   it is (its target, al, the result's extension into its register), its arguments read in their ABI registers.
// - A machine row (TIER1_RULE_ROW, #1284) is its template placed by the claimed placement: the certificate places the
//   template's records itself (each role's register, an /is4 operand's, an opmask decoration's, k1 to k7 renamed) and
//   compares them with the emitted ones. Its roles are registers that hold values (no rsp; k0 no decoration's role, a
//   number only as itself; a pair's registers, VP2INTERSECT's, themselves), and no two share a register but the result
//   and its class's first operand, as Tier 0's placement has them, or operands no record writes. Its records are one
//   ORIGINAL, as a node's; the operands' staging into their roles' registers, the result's move out and the borrowed
//   opmask registers' saves and reloads around it are moves, decoded as any.
// - An asm body (TIER1_RULE_ASM, #1839) is its records placed by the certificate (the operand block's addresses from the
//   frame base register, by the layout) and compared, its own branches exactly the body's; one ORIGINAL that reads no
//   value and kills every register the body touches (IrAsmBody.touched, .masks: the front end's account of the
//   template, trusted as the IR) and the frame bytes its stores write. A record of a form the semantics' tables don't
//   cover writes no register but those. The saves and reloads of the values live in those registers around it are moves.
// - A by-value aggregate (#789): a parameter's copy at entry, a call's argument copies (into staging slots, zmm16 and on,
//   stack slots or the outgoing area), its parts' loads, the hidden pointer, the result parts' stores and the function's
//   own result are regenerated as the emitter writes them (tier1_emit_copy_bytes', tier1_emit_store_part's) and
//   compared; each part is a value of the certificate, defined where its copy puts it and read where it is consumed.
// - Control: each block's tail (its jumps and its edges' moves) is walked for each successor; every path must reach the
//   successor's first record through that edge's moves alone, or reach a successor whose only predecessor it is, whose
//   first records are that edge's moves. Every branch target is a block's or a stub's start.
// - The frame protocol: the prologue and each epilogue are decoded by rsp's offset from its value at entry, record by
//   record, each exactly its form's (push, the frame pointer's set, sub; add, the reset from rbp, pop), and must build
//   the frame the layout's offsets are of (its saved registers pushed, its frame pointer, its allocation exactly) with
//   rsp at a call a multiple of 16; a realigned frame's prologue ends in its `and rsp, -alignment` (#1284), with its
//   frame pointer, and its epilogue resets rsp from rbp. A push is a store of its register's value into its slot, a pop
//   the load back. Each
//   callee-saved register's value at entry and the return address are values of the certificate, asserted at every
//   block's entry, and each return's ret reads them where they were at entry, with rsp back at the return address: a
//   register the function doesn't give back, a slot something overwrote, an epilogue that pops the wrong slot or leaves
//   rsp elsewhere fails.
// - Frame storage: the frame base register's displacements over the spill, save and temporary rows are byte units, so a
//   slot that two values share over their lifetimes is the same bytes, and a node's store into a frame object over
//   them kills what it overwrites. In a realigned frame, whose `and` leaves rsp at no known distance from the entry, the
//   bytes placed from the entry (the pushes' slots, the return address and the incoming area, which rbp reaches) are a
//   second range of units, by their offsets from rsp at entry, and the rows must lie below the pushes. A store through
//   any other pointer is the program's own: it is assumed not to reach the frame's rows, as C's object model says.
// - The prologue may not write any parameter's register, and no record but the prologue and the epilogues may write rsp
//   (but a call's and a ret's own push and pop), the frame base register or the frame pointer.
//
// Not decoded yet, so not certified, each counted by its reason (Tier1CertEmittedReason): stack parameters and arguments
// where the frame base register doesn't reach them, dynamic frames (alloca, a dynamic outgoing area, a realigned frame
// whose frame base register isn't rsp), a probe's prologue, a variadic definition's register save area, the x87's values
// (a long double's, an aggregate's x87 parts), indirect jumps to several labels, an entry block that is a loop's, and a
// machine row of a form x86 semantics' effects don't cover or whose records name a role's register implicitly. The
// allocation's own certificate (tier1_certificate_cfg.c) still covers them. Canonical 32-bit storage (a 4-byte value's upper half zero) is the selection's contract and isn't
// checked: a 4-byte value is its low 4 bytes.
//
// Inputs are borrowed. The program is pushed onto `output`; `scratch` is restored. On failure both are restored.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum Tier1CertEmittedReason
{
    TIER1_CERT_EMITTED_OK,
    // The function has what the decoding doesn't cover: its code isn't certified. AGGREGATE is a parameter's or an
    // argument's place of no kind the decoding knows (by-value aggregates' are decoded since #1284).
    TIER1_CERT_EMITTED_AGGREGATE,
    TIER1_CERT_EMITTED_STACK,
    TIER1_CERT_EMITTED_DYNAMIC,
    TIER1_CERT_EMITTED_VARIADIC,
    TIER1_CERT_EMITTED_INDIRECT,
    TIER1_CERT_EMITTED_ENTRY_LOOP,
    TIER1_CERT_EMITTED_PROBE,
    // A machine row of a form x86 semantics' effects don't cover, or whose records read or write a role's register
    // implicitly (#1284: its one ORIGINAL wouldn't say when); a long double's (its x87 stack).
    TIER1_CERT_EMITTED_VECTOR,
    TIER1_CERT_EMITTED_X87,
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
        "vector",
        "x87",
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

// The registers' locations: each general register's 8 and 4 bytes, each vector register's 64, 32, 16, 8 and 4, each
// opmask register's 1, 2, 4 and 8, then the frame's, made as the stream reads them. Their storage units are the general
// registers' 8 bytes each, then the vector registers': in a function of no vector wider than 8 bytes and no machine row,
// xmm0 to xmm15's 16 each, as Tier 1's floating values have them (the other vector locations name none); in one with
// them (Tier1CertEmittedBuild.wide, #1284), zmm0 to zmm31's 64 each, a write of any width killing the whole register
// (a VEX or EVEX one zeroes it past its width, and a legacy SSE one, which keeps it, writes a register no other value
// shares); then the opmask registers' 8 each; then the frame's.
#define BUSTER_CERT_EMITTED_GPR 0u
#define BUSTER_CERT_EMITTED_VECTOR 32u
#define BUSTER_CERT_EMITTED_MASK 192u
#define BUSTER_CERT_EMITTED_REGISTERS 224u
#define BUSTER_CERT_EMITTED_VECTOR_UNIT 128u
// A kernel's records past which the regeneration's room ends: a fixed rule's sequence, a call's and a node's.
#define BUSTER_CERT_EMITTED_KERNEL (BUSTER_TIER1_SELECT_FIXED_RECORDS + BUSTER_TIER1_SELECT_RECORDS_PER_NODE + 8)
// Source records per node at most: its uses' (up to 15 of a call's, in eights), its clobbers' (16 general, 16 xmm, 8
// opmask registers and a frame range, in eights) and its definition's.
#define BUSTER_CERT_EMITTED_NODE_SOURCES 10u
// A node's clobbers at most: its registers' (16 general, 32 vector and 8 opmask ones) and its frame bytes' locations of
// 64 bytes each (tier1_cert_emitted_kills).
#define BUSTER_CERT_EMITTED_REGISTER_CLOBBERS 56u
#define BUSTER_CERT_EMITTED_CLOBBERS (BUSTER_CERT_EMITTED_REGISTER_CLOBBERS + 256u)

// A call's steps at most: a copy of each part of its arguments (2 each), a load of each register part, the hidden
// pointer, and a store of each result part.
#define BUSTER_CERT_EMITTED_STEPS (2 * BUSTER_TIER1_SELECT_MAX_ARGUMENTS + BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS + 3)
_Static_assert(BUSTER_CERT_EMITTED_STEPS <= UINT8_MAX, "a call's steps count in a byte (Tier1CertEmittedBuild.call_step_count)");

typedef enum Tier1CertEmittedStepKind
{
    // An argument's bytes [from, from + size) from its address into to_base + to: a part's staging slots or its stack slot, or
    // a whole aggregate's slots in the outgoing area (`chunks` values of 64 bytes, the last of what is left).
    TIER1_CERT_STEP_COPY,
    // An argument's 64-byte part from its address into zmm16 and on (`reg`).
    TIER1_CERT_STEP_ZMM,
    // A part into its argument register (`reg`) from its staging slots or its zmm register: `record`.
    TIER1_CERT_STEP_LOAD,
    // The hidden pointer into rdi, the result object's address: `record`.
    TIER1_CERT_STEP_HIDDEN,
    // A result part from its register (`reg`) into the result object, `to` from the frame base register.
    TIER1_CERT_STEP_STORE,
    // At entry: a parameter's part in its incoming slot into the scratch register (`record`; `to` the slot's offset from
    // rsp at entry, by the ABI), then its store into the parameter's copy from its register (`reg`, the scratch register's
    // for a slot's), `to` from the frame base register; and the hidden pointer's into its object, its own storage's
    // (`record`).
    TIER1_CERT_STEP_SLOT,
    TIER1_CERT_STEP_PARAMETER,
    TIER1_CERT_STEP_POINTER,
} Tier1CertEmittedStepKind;

typedef struct Tier1CertEmittedStep Tier1CertEmittedStep;
struct Tier1CertEmittedStep
{
    X86Instruction record;
    // The aggregate's address (a node), a COPY's and a ZMM's.
    uint32_t value;
    // The part's value, or a whole COPY's first; and a whole COPY's count of them (0 for a part's).
    uint32_t pseudo;
    uint32_t chunks;
    uint32_t from;
    uint32_t size;
    int32_t to;
    uint8_t to_base;
    uint8_t kind;
    uint8_t reg;
    uint8_t bytes;
    // The part's value's width: a general register's 8, a vector register's part its bytes (8 for 8 or less), a result's
    // 4 for a float's.
    uint8_t width;
    bool vector;
    // A COPY into a stack slot (a SPLIT location's, or a STACK one's eightbyte), which the call reads there.
    bool slot;
};

typedef struct Tier1CertEmittedBuild Tier1CertEmittedBuild;
struct Tier1CertEmittedBuild
{
    const Tier1SelectPlan* plan;
    const Tier1EmitResult* emitted;
    // Optional per-node scalar definition candidates. They are usable only after the complete decoded program
    // passes tier1_certificate_check; a kernel's matching bytes alone do not prove its operands' provenance.
    Tier1DebugDefinition* debug_definitions;
    Tier1CertEmittedStats* stats;
    Tier1CertLocation* locations;
    uint32_t location_count;
    uint32_t location_capacity;
    // The frame base register's displacements [low, high) are units from `frame_unit`. In a realigned frame (#1284), whose
    // rsp's distance from its value at entry the prologue's `and` leaves unknown, the bytes the protocol places from the
    // entry (the pushes' slots, the return address and the incoming area, which rbp reaches) are a second range of units
    // past those, by their offsets from rsp at entry, [entry_low, entry_high); it is empty in any other frame, whose
    // frame base register's distance from the entry is known, and so are those bytes' displacements from it.
    int32_t low;
    int32_t high;
    int32_t entry_low;
    int32_t entry_high;
    bool realigned;
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
    // return address; and past them each parameter's value as its caller passed it, which a narrow one's extension reads.
    uint32_t pseudo;
    uint32_t incoming;
    // Whether the vector registers are zmm0 to zmm31's 64 bytes each (a function with a vector wider than 8 bytes or a
    // machine row), else xmm0 to xmm15's 16; and the first frame unit, past the registers'.
    bool wide;
    uint32_t frame_unit;
    // By-value aggregates' parts (#789, #1284): the first of the values of the parameters' parts (two each, the hidden
    // pointer's past them), of the function's result parts (two), and each call node's first (`call_first`, by node).
    uint32_t parameter_parts;
    uint32_t result_parts;
    uint32_t* call_first;
    // The entry's steps (its copies), and the next to decode.
    Tier1CertEmittedStep* entry_steps;
    uint32_t entry_count;
    uint32_t entry_next;
    // Each call node's steps (tier1_cert_emitted_call_steps), computed once, by node: its steps (null for none), how many,
    // and how many of them come before its call.
    Tier1CertEmittedStep** call_steps;
    uint8_t* call_step_count;
    uint8_t* call_step_before;
    // The steps of the call node being decoded (`step_node`, BUSTER_IR_NONE for none), the first `step_before` of them
    // before its call, and the next to decode.
    const Tier1CertEmittedStep* steps;
    uint32_t step_count;
    uint32_t step_before;
    uint32_t step_next;
    uint32_t step_node;
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

// A vector width's index among a vector register's locations (64, 32, 16, 8 and 4 bytes), or 5 for no width of one.
static uint32_t tier1_cert_emitted_vector_index(uint32_t width)
{
    return width == 64 ? 0 : width == 32 ? 1 : width == 16 ? 2 : width == 8 ? 3 : width == 4 ? 4 : 5;
}

// Vector register `reg`'s location of `width` bytes, or BUSTER_CERT_NONE where the function's layout has none: a register
// past xmm15, or a width past 16, in a function that isn't wide.
static uint32_t tier1_cert_emitted_vector(const Tier1CertEmittedBuild* build, uint32_t reg, uint32_t width)
{
    const uint32_t index = tier1_cert_emitted_vector_index(width);
    const bool held = index < 5 && reg < (build->wide ? 32u : 16u) && (build->wide || width <= 16);
    return held ? BUSTER_CERT_EMITTED_VECTOR + 5 * reg + index : BUSTER_CERT_NONE;
}

static uint32_t tier1_cert_emitted_mask(uint32_t reg, uint32_t width)
{
    return BUSTER_CERT_EMITTED_MASK + 4 * (reg & 7) + (width == 1 ? 0 : width == 2 ? 1 : width == 4 ? 2 : 3);
}

// A whole vector register's location, which a write to it kills: zmm's 64 bytes in a wide function, else xmm's 16.
static uint32_t tier1_cert_emitted_vector_whole(const Tier1CertEmittedBuild* build, uint32_t reg)
{
    return tier1_cert_emitted_vector(build, reg, build->wide ? 64 : 16);
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

// A narrow parameter's bytes as the ABI passes them (an I1's or I8's 1, an I16's 2), else 0: only those are its caller's,
// in its register's low bytes or its slot's; the rest are whatever the caller left.
static uint32_t tier1_cert_emitted_narrow(uint32_t type)
{
    return type == IR_I1 || type == IR_I8 ? 1 : type == IR_I16 ? 2 : 0;
}

// General register `reg`'s low `width` bytes, as a new location: the caller's bytes before an explicit entry conversion.
static uint32_t tier1_cert_emitted_low(Tier1CertEmittedBuild* build, uint32_t reg, uint32_t width)
{
    uint32_t result = BUSTER_CERT_NONE;
    if (reg < 16 && build->location_count < build->location_capacity)
    {
        result = build->location_count++;
        build->locations[result] = (Tier1CertLocation){
            .unit = 8 * reg,
            .width = (uint16_t)width,
            .extent = (uint16_t)(width == 4 ? 8 : width),
            .class = TIER1_CERT_GPR,
            .reg = (uint8_t)reg,
        };
    }
    return result;
}

// Value `value`'s location in tier1_cfg's register location `where` (class * 32 + register), at its width: a vector's in
// its register's 16, 32 or 64 bytes (#1284), a floating value's in its low 4 or 8.
static uint32_t tier1_cert_emitted_register(const Tier1CertEmittedBuild* build, uint32_t value, uint32_t where)
{
    const Tier1EmitResult* emitted = build->emitted;
    const uint32_t width = tier1_cert_emitted_width(emitted, value);
    const uint32_t class = where / 32;
    const uint32_t reg = where % 32;
    uint32_t result = BUSTER_CERT_NONE;
    if (class == BUSTER_TIER1_ALLOCATE_GPR && reg < 16)
    {
        result = tier1_cert_emitted_gpr(reg, width);
    }
    else if (class == BUSTER_TIER1_ALLOCATE_VECTOR)
    {
        result = tier1_cert_emitted_vector(build, reg, width);
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
            .unit = build->frame_unit + (uint32_t)(displacement - build->low),
            .width = (uint16_t)width,
            .extent = (uint16_t)width,
            .class = TIER1_CERT_STACK,
        };
    }
    return result;
}

// The bytes [offset, offset + width) from rsp at entry, as a new location: the frame base register's where its distance
// from the entry is known, else (a realigned frame, #1284) the entry's own range of units; BUSTER_CERT_NONE where they
// aren't all frame storage.
static uint32_t tier1_cert_emitted_entry_frame(Tier1CertEmittedBuild* build, int64_t offset, uint32_t width)
{
    uint32_t result = BUSTER_CERT_NONE;
    if (!build->realigned)
    {
        result = build->frame_base != INT64_MIN ? tier1_cert_emitted_frame(build, offset - build->frame_base, width) : BUSTER_CERT_NONE;
    }
    else if (width != 0 && width <= BUSTER_CERT_MAX_WIDTH && offset >= build->entry_low && offset + width <= build->entry_high && build->location_count < build->location_capacity)
    {
        result = build->location_count++;
        build->locations[result] = (Tier1CertLocation){
            .unit = build->frame_unit + (uint32_t)((int64_t)build->high - build->low) + (uint32_t)(offset - build->entry_low),
            .width = (uint16_t)width,
            .extent = (uint16_t)width,
            .class = TIER1_CERT_STACK,
        };
    }
    return result;
}

// Where a memory operand from `reg` at `displacement` is frame storage the protocol places: from the frame base register,
// a displacement in its range (`*entry` false); from rbp where it is the frame pointer, or from rsp where the frame base
// register is the frame pointer (the body's rsp, the prologue's), an offset from rsp at entry (`*entry` true). False for
// any other register, a pointer of the program's own.
static bool tier1_cert_emitted_place(const Tier1CertEmittedBuild* build, uint8_t reg, int64_t displacement, int64_t* at, bool* entry)
{
    const bool based = reg == build->base;
    const bool pointer = !based && reg == X86_REGISTER_RBP && build->frame_pointer != INT64_MIN;
    const bool stack = !based && reg == X86_REGISTER_RSP && build->stack != INT64_MIN;
    *entry = pointer || stack;
    *at = based ? displacement : pointer ? build->frame_pointer + displacement : stack ? build->stack + displacement : 0;
    return based || pointer || stack;
}

// A record's memory operand of `width` bytes as a location of the frame's, or BUSTER_CERT_NONE: a base register's
// displacement, without an index or segment, that tier1_cert_emitted_place resolves, all of whose bytes are frame storage.
static uint32_t tier1_cert_emitted_memory(Tier1CertEmittedBuild* build, X86Instruction record, uint32_t width)
{
    int64_t at = 0;
    bool entry = false;
    const bool placed = record.mode == BUSTER_X86_MEMORY_BASE && tier1_cert_emitted_place(build, record.rm, record.displacement, &at, &entry);
    return !placed ? BUSTER_CERT_NONE : entry ? tier1_cert_emitted_entry_frame(build, at, width) : tier1_cert_emitted_frame(build, at, width);
}

// Whether a record's memory operand is the frame's: a displacement, without an index or segment, from a register
// tier1_cert_emitted_place resolves; `*at` its frame base register's displacement or (`*entry`) its offset from rsp at
// entry. An indexed access through a frame object's address stays in the object, as C's object model says.
static bool tier1_cert_emitted_frame_operand(const Tier1CertEmittedBuild* build, X86Instruction record, int64_t* at, bool* entry)
{
    return record.mode == BUSTER_X86_MEMORY_BASE && tier1_cert_emitted_place(build, record.rm, record.displacement, at, entry);
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

// The vector class's moves (#1284): each form's width, whether it copies its rm operand into its reg one (a load, or a
// register's copy) or its reg one into its rm operand (a store), whether its rm operand may be a register or the frame's
// memory, and the registers its encoding reaches (a legacy or VEX form's 16, an EVEX one's 32). A floating value's
// movaps between registers and its movss or movsd to and from its slot; a vector's vmovdqa64 between registers and
// vmovdqu32 to and from its slot (#141); a by-value aggregate's part's movsd, movups, vmovdqu and vmovdqu64 to and from
// its staging slots and between registers (tier1_select_part_form, #789). movss's and movsd's moves between registers
// merge, and aren't moves of the CFG consumer's.
typedef struct Tier1CertEmittedVectorMove Tier1CertEmittedVectorMove;
struct Tier1CertEmittedVectorMove
{
    uint16_t form;
    uint8_t width;
    bool load;
    bool direct;
    bool memory;
    uint8_t registers;
};

static const Tier1CertEmittedVectorMove tier1_cert_emitted_vector_moves[] = {
    { X86_FORM_MOVAPS_X_XM, 16, true, true, false, 16 },
    { X86_FORM_MOVSS_X_XM32, 4, true, false, true, 16 },
    { X86_FORM_MOVSS_XM32_X, 4, false, false, true, 16 },
    { X86_FORM_MOVSD_X_XM64, 8, true, false, true, 16 },
    { X86_FORM_MOVSD_XM64_X, 8, false, false, true, 16 },
    { X86_FORM_MOVUPS_X_XM, 16, true, true, true, 16 },
    { X86_FORM_MOVUPS_XM_X, 16, false, false, true, 16 },
    { X86_FORM_VMOVDQU_Y_YM256, 32, true, true, true, 16 },
    { X86_FORM_VMOVDQU_YM256_Y, 32, false, false, true, 16 },
    { X86_FORM_VMOVDQA64_X_XM128, 16, true, true, false, 32 },
    { X86_FORM_VMOVDQA64_Y_YM256, 32, true, true, false, 32 },
    { X86_FORM_VMOVDQA64_Z_ZM512, 64, true, true, false, 32 },
    { X86_FORM_VMOVDQU32_X_XM128, 16, true, false, true, 32 },
    { X86_FORM_VMOVDQU32_XM128_X, 16, false, false, true, 32 },
    { X86_FORM_VMOVDQU32_Y_YM256, 32, true, false, true, 32 },
    { X86_FORM_VMOVDQU32_YM256_Y, 32, false, false, true, 32 },
    { X86_FORM_VMOVDQU32_Z_ZM512, 64, true, false, true, 32 },
    { X86_FORM_VMOVDQU32_ZM512_Z, 64, false, false, true, 32 },
    { X86_FORM_VMOVDQU64_Z_ZM512, 64, true, true, true, 32 },
    { X86_FORM_VMOVDQU64_ZM512_Z, 64, false, false, true, 32 },
    // An 8-byte SSE carrier's copy (#1280, tier1_select_carrier_move), the rest of its destination zeroed.
    { X86_FORM_VMOVQ_X_XM64_EVEX, 8, true, true, false, 32 },
    // At a target without AVX512VL (#1281), their VEX twins, below 16 alone: a vector's vmovdqa copies and vmovdqu loads,
    // stores, spills and reloads (tier1_select_vector_forms and tier1_select_class_forms with `vex`), and a carrier's
    // vmovq.
    { X86_FORM_VMOVDQA_X_XM128, 16, true, true, false, 16 },
    { X86_FORM_VMOVDQA_Y_YM256, 32, true, true, false, 16 },
    { X86_FORM_VMOVDQU_X_XM128, 16, true, false, true, 16 },
    { X86_FORM_VMOVDQU_XM128_X, 16, false, false, true, 16 },
    { X86_FORM_VMOVQ_X_XM64, 8, true, true, false, 16 },
};

static const Tier1CertEmittedVectorMove* tier1_cert_emitted_vector_move(uint16_t form)
{
    const Tier1CertEmittedVectorMove* result = 0;
    for (uint32_t k = 0; result == 0 && k < BUSTER_ARRAY_LENGTH(tier1_cert_emitted_vector_moves); k += 1)
    {
        result = tier1_cert_emitted_vector_moves[k].form == form ? &tier1_cert_emitted_vector_moves[k] : 0;
    }
    return result;
}

// Record `r`'s move, as an edit: its form's width, between its registers and frame bytes. A recomputation is decoded
// where `node` (its origin's value) has a recipe whose copy is exactly the record.
static bool tier1_cert_emitted_move(Tier1CertEmittedBuild* build, uint32_t r, uint32_t node, Tier1CertInstruction* edit)
{
    const Tier1EmitResult* emitted = build->emitted;
    const X86Instruction record = emitted->code.records[r];
    const bool direct = record.mode == BUSTER_X86_REGISTER_DIRECT;
    const bool memory = record.mode == BUSTER_X86_MEMORY_BASE;
    uint32_t a = BUSTER_CERT_NONE;
    uint32_t b = BUSTER_CERT_NONE;
    // A general move: its load's or store's width, 8 or 4.
    const uint16_t form = record.form;
    const bool load64 = form == X86_FORM_MOV_R64_RM64;
    const bool load32 = form == X86_FORM_MOV_R32_RM32;
    const bool store64 = form == X86_FORM_MOV_RM64_R64;
    const bool store32 = form == X86_FORM_MOV_RM32_R32;
    const bool general = (load64 || load32 || store64 || store32) && (direct || memory) && record.reg < 16;
    const Tier1CertEmittedVectorMove* vector = general ? 0 : tier1_cert_emitted_vector_move(form);
    // A 4-byte SSE carrier's copy (#1280, tier1_select_carrier_move): vinsertps of the source's element 0 into element 0,
    // elements 1 to 3 zeroed (immediate 0x0e), the source its vvvv too: a 4-byte move, the rest of its destination zeroed.
    // Its VEX twin at a target without AVX512VL (#1281) names registers below 16 alone.
    const uint8_t insert_registers = form == X86_FORM_VINSERTPS_X_X_XM32_IMM8_EVEX ? 32 : form == X86_FORM_VINSERTPS_X_X_XM32_IMM8 ? 16 : 0;
    const bool insert = insert_registers != 0 && direct && record.immediate == 0x0e && record.vvvv == record.rm && record.reg < insert_registers && record.rm < insert_registers;
    if (general)
    {
        const uint32_t width = load64 || store64 ? 8 : 4;
        const uint32_t rm = direct ? (record.rm < 16 ? tier1_cert_emitted_gpr(record.rm, width) : BUSTER_CERT_NONE) : tier1_cert_emitted_memory(build, record, width);
        a = load64 || load32 ? tier1_cert_emitted_gpr(record.reg, width) : rm;
        b = load64 || load32 ? rm : tier1_cert_emitted_gpr(record.reg, width);
    }
    else if (vector != 0 && ((direct && vector->direct) || (memory && vector->memory)) && record.reg < vector->registers && (!direct || record.rm < vector->registers))
    {
        // The vector class's (#1284): a floating value's movaps and movss or movsd, a vector's vmovdqa64 and vmovdqu32 (or
        // vmovdqa and vmovdqu, #1281), an SSE carrier's vmovq (#1280), a part's movsd, movups, vmovdqu or vmovdqu64
        // (#789), at their forms' widths. A load from memory zeroes the rest of
        // an xmm register (its scalar view's write extent), and in a wide function every write kills the whole register.
        const uint32_t rm = direct ? tier1_cert_emitted_vector(build, record.rm, vector->width) : tier1_cert_emitted_memory(build, record, vector->width);
        a = vector->load ? tier1_cert_emitted_vector(build, record.reg, vector->width) : rm;
        b = vector->load ? rm : tier1_cert_emitted_vector(build, record.reg, vector->width);
    }
    else if (insert)
    {
        a = tier1_cert_emitted_vector(build, record.reg, 4);
        b = tier1_cert_emitted_vector(build, record.rm, 4);
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
            else if (form == forms->load && memory && record.reg < 8)
            {
                a = tier1_cert_emitted_mask(record.reg, width);
                b = tier1_cert_emitted_memory(build, record, width);
            }
            else if (form == forms->store && memory && record.reg < 8)
            {
                a = tier1_cert_emitted_memory(build, record, width);
                b = tier1_cert_emitted_mask(record.reg, width);
            }
        }
    }
    // A move's fields but its registers, its mode and a memory operand's displacement are zero (but a carrier's vinsertps'
    // immediate and vvvv, checked above).
    const bool exact = (insert || (record.immediate == 0 && record.vvvv == 0)) && record.index == 0 && record.decorations == 0 && (!direct || record.displacement == 0);
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
    const bool found = value != BUSTER_TIER1_CFG_NONE && tier1_cfg_location_oracle(&emitted->cfg, value, position, &where, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER;
    return found ? where : UINT32_MAX;
}

// Node `node`'s definition's register, where the allocation's interval starting at its definition phase has it, or
// UINT32_MAX.
static uint32_t tier1_cert_emitted_definition(const Tier1EmitResult* emitted, uint32_t node, uint32_t position)
{
    const Tier1CfgAllocation* cfg = &emitted->cfg;
    const uint32_t value = emitted->node_values[node];
    const uint32_t interval = value != BUSTER_TIER1_CFG_NONE ? tier1_cfg_interval_oracle(cfg, value, position) : BUSTER_TIER1_CFG_NONE;
    const uint32_t fragment = interval != BUSTER_TIER1_CFG_NONE ? cfg->allocation.fragment_offsets[interval] : 0;
    const bool found = interval != BUSTER_TIER1_CFG_NONE && cfg->intervals.starts[interval] == position && cfg->allocation.kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER;
    return found ? 32 * emitted->cfg_input.classes[value] + cfg->allocation.registers[fragment] : UINT32_MAX;
}

// A node's uses at most: a call's arguments and its callee, and its by-value aggregates' parts (#789): its register and
// stack slots' parts and the hidden pointer (a whole aggregate's bytes are read by an ORIGINAL of their own,
// tier1_cert_emitted_wholes); a return's protocol values and result parts.
#define BUSTER_CERT_EMITTED_USES (BUSTER_TIER1_SELECT_MAX_ARGUMENTS + 2 + BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS + 1)

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
    uint32_t uses[BUSTER_CERT_EMITTED_USES];
    uint32_t use_locations[BUSTER_CERT_EMITTED_USES];
    uint32_t use_count;
    uint32_t definition;
    uint32_t definition_location;
    // A call's aggregate result's parts (#789, #1284), which it defines in their registers after it: up to a split
    // vector's four (#1624, tier1_select_result_parts).
    uint32_t results[BUSTER_TIER1_SELECT_RESULT_PARTS];
    uint32_t result_locations[BUSTER_TIER1_SELECT_RESULT_PARTS];
    uint32_t result_count;
    // A call's outgoing area's bytes from rsp, which its callee owns: killed after it.
    uint32_t outgoing;
    // A call's: every register the ABI lets its callee change is killed after its arguments are read.
    bool call;
    // An asm body's (#1839, TIER1_RULE_ASM): its records of forms x86 semantics doesn't cover write only memory, and every
    // register it touches (IrAsmBody.touched, .masks) is killed after it, as the front end's account of its template.
    const IrAsmBody* body;
};

// A use of `value` at `location`: one for each distinct pair, so that a value read at two locations (a call's argument
// passed twice, in two registers) is proved at both.
static bool tier1_cert_emitted_add_use(Tier1CertEmittedKernel* kernel, uint32_t value, uint32_t location)
{
    bool fresh = true;
    for (uint32_t k = 0; fresh && k < kernel->use_count; k += 1)
    {
        fresh = kernel->uses[k] != value || kernel->use_locations[k] != location;
    }
    if (fresh && kernel->use_count < BUSTER_ARRAY_LENGTH(kernel->uses))
    {
        kernel->uses[kernel->use_count] = value;
        kernel->use_locations[kernel->use_count] = location;
        kernel->use_count += 1;
    }
    return location != BUSTER_CERT_NONE && (!fresh || kernel->use_count <= BUSTER_ARRAY_LENGTH(kernel->uses));
}

// ---------------------------------------------------------------------------------------------------------------------
// By-value aggregates (#789, #1284)
//
// A by-value aggregate's ABI record makes the CFG consumer copy its parts: a parameter's from its registers (or its
// incoming slot) into its copy at entry, a call argument's from its address into staging slots, zmm16 and
// on (a 64-byte part) or its stack slots, and from there into its argument register, a call's result from its registers
// into the result's object, and the function's own result from its address into its result registers, or through the
// hidden pointer. Each part is a value of the certificate (a pseudo value past the function's), defined where its copy
// puts it and read where its consumer reads it, so that the checker proves it reaches its register, or its slot,
// unchanged. The records are regenerated from the IR's records, the allocation's registers and the frame's layout, and
// must equal the emitted ones. How the bytes are copied (tier1_emit_copy_bytes's pieces, tier1_emit_store_part's) is the
// selection's, trusted as a kernel's sequence is. A whole aggregate's bytes in the outgoing area are values of 64 bytes
// each; a part's padding (an eightbyte's bytes past the aggregate's) is the callee's to ignore, as the ABI says.

static bool tier1_cert_emitted_exactly(X86Instruction record, X86Instruction expected);
static bool tier1_cert_emitted_original(Tier1CertEmittedBuild* build, uint32_t node, uint32_t kind, uint32_t chunk, Tier1CertSource** record, Tier1CertInstruction** instruction);
static bool tier1_cert_emitted_originals(Tier1CertEmittedBuild* build, uint32_t node, const Tier1CertEmittedKernel* kernel, const X86Instruction* records, uint32_t count);

// Piece `k` of a copy of `size` bytes from `from_base` + `from` to `to_base` + `to` through general register `data`, its
// read and its write, as tier1_emit_copy_bytes makes them; and how many pieces it makes.
static uint32_t tier1_cert_emitted_piece_bytes(uint32_t size)
{
    return size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
}

static uint32_t tier1_cert_emitted_pieces(uint32_t size)
{
    const uint32_t piece = tier1_cert_emitted_piece_bytes(size);
    return size / piece + (size % piece != 0 ? 1u : 0u);
}

static void tier1_cert_emitted_piece(uint8_t data, uint8_t to_base, int32_t to, uint8_t from_base, int32_t from, uint32_t size, uint32_t k, X86Instruction* read, X86Instruction* write)
{
    const uint32_t piece = tier1_cert_emitted_piece_bytes(size);
    const uint16_t load = piece == 8 ? X86_FORM_MOV_R64_RM64 : piece == 4 ? X86_FORM_MOV_R32_RM32 : piece == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
    const uint32_t whole = size / piece;
    const int32_t offset = (int32_t)(k < whole ? piece * k : size - piece);
    *read = (X86Instruction){ .displacement = from + offset, .reg = data, .rm = from_base, .mode = BUSTER_X86_MEMORY_BASE, .form = load };
    *write = (X86Instruction){ .displacement = to + offset, .reg = data, .rm = to_base, .mode = BUSTER_X86_MEMORY_BASE, .form = tier1_emit_store_form(piece) };
}

// The records of a store of general register `reg`'s low `bytes` (1 to 8) at `base` + `displacement`, as
// tier1_emit_store_part makes them, into `records` (5 at most); 0 for none.
static uint32_t tier1_cert_emitted_store_part(uint8_t reg, uint32_t bytes, uint8_t base, int32_t displacement, X86Instruction* records)
{
    uint32_t count = 0;
    uint32_t done = 0;
    for (uint32_t step = 0; bytes != 0 && bytes <= 8 && step < 3 && done < bytes; step += 1)
    {
        const uint32_t left = bytes - done;
        const uint32_t piece = left >= 8 ? 8 : left >= 4 ? 4 : left >= 2 ? 2 : 1;
        records[count++] = (X86Instruction){ .displacement = displacement + (int32_t)done, .reg = reg, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = tier1_emit_store_form(piece) };
        done += piece;
        if (done != bytes)
        {
            records[count++] = (X86Instruction){ .form = X86_FORM_SHR_RM64_IMM8, .rm = reg, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = (int32_t)(8 * piece) };
        }
    }
    return bytes != 0 && done == bytes ? count : 0;
}

// A call's aggregate result's object (#789): its into operand's frame object, at `*displacement` from the frame base
// register.
static bool tier1_cert_emitted_into(const Tier1CertEmittedBuild* build, uint32_t node, int32_t* displacement)
{
    const IrFunction* function = build->plan->function;
    const uint32_t into = function->c[node];
    return into < function->instruction_count && function->op[into] == IR_STACK_ADDR && tier1_emit_cfg_object(function, build->emitted, function->a[into], displacement);
}

// The width of a value of a vector register's part of `bytes`: the part's form's (tier1_select_part_form: movsd's 8 for
// 8 or less), a result's 4 for a float's (movss's store).
static uint8_t tier1_cert_emitted_part_width(bool vector, uint32_t bytes, bool result)
{
    return (uint8_t)(!vector ? 8 : result && bytes == 4 ? 4 : bytes <= 8 ? 8 : bytes);
}

// Call node `node`'s steps (`*count` of them, into `steps`), in the CFG consumer's order: each by-value aggregate argument's
// copies (tier1_emit_cfg_records'), its register parts' loads, the hidden pointer's lea, which go before the call (the
// first `*before`), and its result parts' stores after it (tier1_emit_call's); their values from `first`, `*pseudos` of
// them. False where the shapes are past what the consumer emits.
static bool tier1_cert_emitted_call_steps(const Tier1CertEmittedBuild* build, uint32_t node, uint32_t first, Tier1CertEmittedStep* steps, uint32_t* count, uint32_t* before, uint32_t* pseudos)
{
    const IrFunction* function = build->plan->function;
    const IrCall* call = &function->calls[function->b[node]];
    const IrAbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1] : 0;
    const uint32_t offset = build->plan->effective_use_offsets[node];
    Tier1CertEmittedStep loads[BUSTER_ABI_INTEGER_ARGUMENTS + BUSTER_ABI_VECTOR_ARGUMENTS];
    uint32_t load_count = 0;
    Tier1SelectPlaces places = { 0 };
    uint32_t staged = 0;
    uint32_t staged_zmm = 0;
    uint32_t pseudo = first;
    uint32_t n = 0;
    bool valid = call->argument_count <= BUSTER_TIER1_SELECT_MAX_ARGUMENTS;
    // A call without ABI records has no aggregate argument or result.
    for (uint32_t argument = 0; valid && abi != 0 && argument < call->argument_count; argument += 1)
    {
        const Tier1SelectPlace place = tier1_select_argument_place(function, call, argument, &places);
        const IrPassed* record = place.kind == BUSTER_TIER1_PLACE_AGGREGATE ? tier1_select_argument_record(function, call, argument) : 0;
        const uint32_t value = build->plan->effective_use_values[offset + argument];
        Tier1SelectPart parts[2];
        const bool whole = record != 0 && tier1_select_location_whole(record->location);
        const uint32_t part_count = record != 0 && !whole ? tier1_select_location_parts(record->location, record->size, parts) : 0;
        if (whole)
        {
            valid = n < BUSTER_CERT_EMITTED_STEPS && record->location.stack_offset <= INT32_MAX && record->size != 0;
            steps[valid ? n : 0] = (Tier1CertEmittedStep){
                .value = value,
                .pseudo = pseudo,
                .chunks = (record->size + BUSTER_CERT_MAX_WIDTH - 1) / BUSTER_CERT_MAX_WIDTH,
                .size = record->size,
                .to = (int32_t)record->location.stack_offset,
                .to_base = X86_REGISTER_RSP,
                .kind = TIER1_CERT_STEP_COPY,
                .slot = true,
            };
            pseudo += valid ? steps[n].chunks : 0;
            n += valid;
        }
        for (uint32_t k = 0; valid && k < part_count; k += 1)
        {
            const Tier1SelectPart part = parts[k];
            const bool slot = part.reg == BUSTER_ABI_REGISTER_NONE;
            const bool vector = !slot && (part.reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
            const bool zmm = !slot && tier1_select_part_in_zmm(part.bytes);
            const uint32_t slots = slot ? 0 : tier1_select_argument_part_slots(part.bytes);
            const uint32_t at = zmm ? BUSTER_TIER1_SELECT_PART_ZMM + staged_zmm : staged;
            const uint8_t width = tier1_cert_emitted_part_width(vector, part.bytes, false);
            const Tier1EmitInto storage = slot ? (Tier1EmitInto){ .displacement = (int32_t)record->location.stack_offset, .base = X86_REGISTER_RSP, .valid = true } :
                                               zmm ? (Tier1EmitInto){ .valid = true } : tier1_emit_argument_staging(build->emitted, staged, part.bytes);
            valid = n < BUSTER_CERT_EMITTED_STEPS && (slot || load_count < BUSTER_ARRAY_LENGTH(loads)) && record->location.stack_offset <= INT32_MAX &&
                    (zmm ? staged_zmm < BUSTER_ABI_VECTOR_ARGUMENTS : storage.valid);
            steps[valid ? n : 0] = (Tier1CertEmittedStep){
                .value = value,
                .pseudo = pseudo,
                .from = part.offset,
                .size = part.bytes,
                .to = storage.displacement,
                .to_base = storage.base,
                .kind = zmm ? TIER1_CERT_STEP_ZMM : TIER1_CERT_STEP_COPY,
                .reg = (uint8_t)(zmm ? at : 0),
                .bytes = part.bytes,
                .width = zmm ? 64 : slot ? 8 : width,
                .vector = vector,
                .slot = slot,
            };
            if (valid && !slot)
            {
                loads[load_count++] = (Tier1CertEmittedStep){
                    .record = {
                        .displacement = storage.displacement,
                        .reg = (uint8_t)(part.reg & 0x1f),
                        .rm = zmm ? (uint8_t)at : storage.base,
                        .mode = zmm ? BUSTER_X86_REGISTER_DIRECT : BUSTER_X86_MEMORY_BASE,
                        .form = vector ? tier1_select_part_form(part.bytes, true) : X86_FORM_MOV_R64_RM64,
                    },
                    .pseudo = pseudo,
                    .kind = TIER1_CERT_STEP_LOAD,
                    .reg = (uint8_t)(part.reg & 0x1f),
                    .bytes = part.bytes,
                    .width = zmm ? 64 : width,
                    .vector = vector,
                };
            }
            staged += slots;
            staged_zmm += zmm ? 1u : 0u;
            pseudo += 1;
            n += valid;
        }
    }
    for (uint32_t k = 0; valid && k < load_count; k += 1)
    {
        valid = n < BUSTER_CERT_EMITTED_STEPS;
        steps[valid ? n : 0] = loads[k];
        n += valid;
    }
    const bool hidden = abi != 0 && abi->result_size != 0 && abi->call.result.kind == BUSTER_ABI_RESULT_MEMORY;
    const bool returned = abi != 0 && abi->result_size != 0 && abi->call.result.kind == BUSTER_ABI_RESULT_REGISTERS;
    int32_t into = 0;
    valid = valid && ((!hidden && !returned) || tier1_cert_emitted_into(build, node, &into));
    if (valid && hidden)
    {
        valid = n < BUSTER_CERT_EMITTED_STEPS;
        steps[valid ? n : 0] = (Tier1CertEmittedStep){
            .record = { .displacement = into, .reg = X86_REGISTER_RDI, .rm = build->base, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_LEA_R64_M },
            .pseudo = pseudo,
            .kind = TIER1_CERT_STEP_HIDDEN,
            .reg = X86_REGISTER_RDI,
            .width = 8,
        };
        pseudo += 1;
        n += valid;
    }
    *before = n;
    Tier1SelectPart results[BUSTER_TIER1_SELECT_RESULT_PARTS];
    const uint32_t result_count = returned ? tier1_select_result_parts(abi->call.result, abi->result_size, results) : 0;
    valid = valid && (!returned || result_count != 0);
    for (uint32_t k = 0; valid && k < result_count; k += 1)
    {
        const bool vector = (results[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        valid = n < BUSTER_CERT_EMITTED_STEPS;
        steps[valid ? n : 0] = (Tier1CertEmittedStep){
            .pseudo = pseudo,
            .to = into + results[k].offset,
            .kind = TIER1_CERT_STEP_STORE,
            .reg = (uint8_t)(results[k].reg & 0x1f),
            .bytes = results[k].bytes,
            .width = tier1_cert_emitted_part_width(vector, results[k].bytes, true),
            .vector = vector,
        };
        pseudo += 1;
        n += valid;
    }
    *count = n;
    *pseudos = pseudo - first;
    return valid;
}

// The entry's steps (tier1_emit_cfg_entry's copies, in its order): each by-value aggregate parameter's parts into its
// copy, a part in an incoming slot through the scratch register, and the hidden pointer into its object; their values
// from `first`, two per parameter (the hidden pointer's past them).
static bool tier1_cert_emitted_entry_steps(const Tier1CertEmittedBuild* build, uint32_t first, Tier1CertEmittedStep* steps, uint32_t capacity, uint32_t* count)
{
    const IrFunction* function = build->plan->function;
    const Tier1EmitResult* emitted = build->emitted;
    uint32_t n = 0;
    bool valid = true;
    for (uint32_t p = 0; valid && function->parameter_passed != 0 && p <= function->parameter_count; p += 1)
    {
        const IrPassed* record = p < function->parameter_count ? tier1_select_parameter_record(function, p) : 0;
        const bool copied = tier1_emit_cfg_record_copied(record);
        const bool hidden = p == function->parameter_count && function->result_size != 0 && function->result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
        Tier1SelectPart parts[2] = { { .reg = BUSTER_ABI_RDI, .offset = 0, .bytes = 8 } };
        const uint32_t part_count = copied ? tier1_select_location_parts(record->location, record->size, parts) : hidden ? 1 : 0;
        int32_t offset = 0;
        valid = (!copied && !hidden) || (part_count != 0 && tier1_emit_cfg_object(function, emitted, function->slot_count + p, &offset));
        for (uint32_t k = 0; valid && k < part_count; k += 1)
        {
            const bool slot = parts[k].reg == BUSTER_ABI_REGISTER_NONE;
            const bool vector = !slot && (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
            const uint8_t reg = slot ? (uint8_t)BUSTER_TIER1_SELECT_SCRATCH : (uint8_t)(parts[k].reg & 0x1f);
            uint8_t base = X86_REGISTER_RSP;
            int32_t incoming = 0;
            valid = n + 2 <= capacity && (!slot || tier1_emit_cfg_incoming(emitted, record->location.stack_offset, &base, &incoming));
            if (valid && slot)
            {
                steps[n++] = (Tier1CertEmittedStep){
                    .record = { .displacement = incoming, .reg = BUSTER_TIER1_SELECT_SCRATCH, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 },
                    .pseudo = first + 2 * p + k,
                    .to = (int32_t)(8 + record->location.stack_offset),
                    .kind = TIER1_CERT_STEP_SLOT,
                    .width = 8,
                };
            }
            if (valid)
            {
                steps[n++] = (Tier1CertEmittedStep){
                    .record = {
                        .displacement = offset + parts[k].offset,
                        .reg = reg,
                        .rm = build->base,
                        .mode = BUSTER_X86_MEMORY_BASE,
                        .form = vector ? tier1_select_part_form(parts[k].bytes, false) : X86_FORM_MOV_RM64_R64,
                    },
                    .pseudo = first + 2 * p + k,
                    .kind = hidden ? TIER1_CERT_STEP_POINTER : TIER1_CERT_STEP_PARAMETER,
                    .reg = reg,
                    .bytes = parts[k].bytes,
                    .width = tier1_cert_emitted_part_width(vector, parts[k].bytes, false),
                    .vector = vector,
                    .slot = slot,
                };
            }
        }
    }
    *count = n;
    return valid;
}

// Record `r`'s move, as an edit onto the stream (tier1_cert_emitted_move), writing `destination` where it isn't
// BUSTER_CERT_NONE.
static bool tier1_cert_emitted_append_move(Tier1CertEmittedBuild* build, uint32_t r, uint32_t node, uint32_t destination)
{
    bool valid = build->code_count < build->code_capacity && tier1_cert_emitted_move(build, r, node, build->code + build->code_count);
    valid = valid && (destination == BUSTER_CERT_NONE || build->code[build->code_count].a == destination);
    build->code_count += valid;
    build->stats->edits += valid;
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_DECODE, r);
}

// An ORIGINAL that defines `value` at `location` alone: a part's value where its copy put it.
static bool tier1_cert_emitted_define(Tier1CertEmittedBuild* build, uint32_t node, uint32_t value, uint32_t location)
{
    Tier1CertSource* source = 0;
    Tier1CertInstruction* instruction = 0;
    const bool placed = location != BUSTER_CERT_NONE;
    const bool valid = placed && tier1_cert_emitted_original(build, node, 2, 0, &source, &instruction);
    if (valid)
    {
        source->operands[0] = (Tier1CertOperand){
            .value = value,
            .allowed_registers = UINT32_MAX,
            .flags = build->locations[location].class == TIER1_CERT_STACK ? BUSTER_CERT_MEMORY : 0,
        };
        source->def_count = 1;
        instruction->locations[0] = location;
        instruction->operand_count = 1;
        build->stats->originals += 1;
    }
    return valid || tier1_cert_emitted_refuse(build, placed ? TIER1_CERT_EMITTED_CAPACITY : TIER1_CERT_EMITTED_DECODE, UINT32_MAX);
}

// The bytes at rsp + `displacement` as a location of the frame's.
static uint32_t tier1_cert_emitted_stack(Tier1CertEmittedBuild* build, int64_t displacement, uint32_t width)
{
    const X86Instruction record = { .displacement = (int32_t)displacement, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
    return displacement >= INT32_MIN && displacement <= INT32_MAX ? tier1_cert_emitted_memory(build, record, width) : BUSTER_CERT_NONE;
}

// A step's records from record `*r` (the node's records of its origin up to `end`), exactly the regenerated ones, as edits
// and ORIGINALs: a copy's address in the scratch register (reloaded or recomputed) where the allocation has it in no
// register, rax saved in the red zone's save slot while a copy goes through it, the copy's pieces reading the address and
// defining the part's value where they put it; a load's move; the hidden pointer's lea defining its value; a store reading
// the result part's.
static bool tier1_cert_emitted_step(Tier1CertEmittedBuild* build, uint32_t node, const Tier1CertEmittedStep* step, uint32_t* r, uint32_t end)
{
    const Tier1EmitResult* emitted = build->emitted;
    const X86Instruction* records = emitted->code.records;
    const uint32_t use = build->base_position + 2 * node;
    const bool copies = step->kind == TIER1_CERT_STEP_COPY || step->kind == TIER1_CERT_STEP_ZMM;
    const uint32_t value = copies ? emitted->node_values[step->value] : BUSTER_TIER1_CFG_NONE;
    const uint32_t where = copies ? tier1_cert_emitted_use(emitted, value, use) : UINT32_MAX;
    const bool registered = where != UINT32_MAX && where / 32 == BUSTER_TIER1_ALLOCATE_GPR && where % 32 < 16;
    const uint8_t source = registered ? (uint8_t)(where % 32) : (uint8_t)BUSTER_TIER1_SELECT_SCRATCH;
    uint32_t at = *r;
    // An address the allocation has in the scratch register is refused: a copy's pieces load through it into it.
    bool valid = !copies || (value != BUSTER_TIER1_CFG_NONE && (!registered || source != BUSTER_TIER1_SELECT_SCRATCH));
    // The address in the scratch register (tier1_emit_cfg_aggregate_address): its reload from its slot, or its
    // recomputation, which the edit's decoding checks is its value's.
    if (valid && copies && !registered)
    {
        valid = at < end && tier1_cert_emitted_append_move(build, at, step->value, tier1_cert_emitted_gpr(BUSTER_TIER1_SELECT_SCRATCH, 8));
        at += 1;
    }
    X86Instruction expected[8];
    uint32_t expected_count = 0;
    Tier1CertEmittedKernel kernel = {
        .referenced = UINT32_MAX,
        .definition = BUSTER_TIER1_CFG_NONE,
        .definition_location = BUSTER_CERT_NONE,
    };
    if (valid && step->kind == TIER1_CERT_STEP_COPY)
    {
        // Through the scratch register, or rax where the scratch register holds the address, optionally saved meanwhile.
        const uint8_t data = registered ? (uint8_t)BUSTER_TIER1_SELECT_SCRATCH : (uint8_t)X86_REGISTER_RAX;
        const int32_t save = tier1_emit_staging(BUSTER_TIER1_EMIT_STAGING_SAVE);
        const X86Instruction saved = { .displacement = save, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_RM64_R64 };
        const X86Instruction restored = { .displacement = save, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 };
        // Preservation is optional when rax holds no live value. Derive the pair from the actual records, not the
        // emitter's liveness claim: without it the copy's rax writes remain clobbers, so a later use must prove itself.
        const bool preserved = !registered && at < end && tier1_cert_emitted_exactly(records[at], saved);
        valid = !preserved || tier1_cert_emitted_append_move(build, at, BUSTER_IR_NONE, BUSTER_CERT_NONE);
        at += preserved ? 1 : 0;
        const uint32_t pieces = tier1_cert_emitted_pieces(step->size);
        valid = valid && step->from <= INT32_MAX && end >= at && end - at >= 2 * pieces;
        for (uint32_t k = 0; valid && k < pieces; k += 1)
        {
            X86Instruction read;
            X86Instruction write;
            tier1_cert_emitted_piece(data, step->to_base, step->to, source, (int32_t)step->from, step->size, k, &read, &write);
            valid = tier1_cert_emitted_exactly(records[at + 2 * k], read) && tier1_cert_emitted_exactly(records[at + 2 * k + 1], write);
            valid = valid && tier1_cert_emitted_take_reference(build, at + 2 * k, 0) && tier1_cert_emitted_take_reference(build, at + 2 * k + 1, 0);
        }
        valid = valid && tier1_cert_emitted_add_use(&kernel, value, tier1_cert_emitted_gpr(source, 8)) && tier1_cert_emitted_originals(build, node, &kernel, records + at, 2 * pieces);
        at += valid ? 2 * pieces : 0;
        // The part's value where the copy put it; a whole aggregate's, 64 bytes at a time.
        for (uint32_t j = 0; valid && j < (step->chunks != 0 ? step->chunks : 1); j += 1)
        {
            const uint32_t left = step->size - BUSTER_CERT_MAX_WIDTH * j;
            const uint32_t width = step->chunks != 0 ? (left < BUSTER_CERT_MAX_WIDTH ? left : BUSTER_CERT_MAX_WIDTH) : step->width;
            const X86Instruction storage = { .displacement = step->to + (int32_t)(BUSTER_CERT_MAX_WIDTH * j), .rm = step->to_base, .mode = BUSTER_X86_MEMORY_BASE };
            valid = tier1_cert_emitted_define(build, node, step->pseudo + j, tier1_cert_emitted_memory(build, storage, width));
        }
        valid = valid && (!preserved || (at < end && tier1_cert_emitted_exactly(records[at], restored) && tier1_cert_emitted_append_move(build, at, BUSTER_IR_NONE, tier1_cert_emitted_gpr(X86_REGISTER_RAX, 8))));
        at += preserved ? 1 : 0;
    }
    else if (valid && step->kind == TIER1_CERT_STEP_ZMM)
    {
        expected[expected_count++] = (X86Instruction){ .displacement = (int32_t)step->from, .reg = step->reg, .rm = source, .mode = BUSTER_X86_MEMORY_BASE, .form = tier1_select_part_form(64, true) };
        valid = tier1_cert_emitted_add_use(&kernel, value, tier1_cert_emitted_gpr(source, 8));
    }
    else if (valid && (step->kind == TIER1_CERT_STEP_LOAD || step->kind == TIER1_CERT_STEP_SLOT || step->kind == TIER1_CERT_STEP_POINTER))
    {
        // A move of the part's value: into its argument register, an incoming slot's into the scratch register, the hidden
        // pointer into its object.
        valid = at < end && tier1_cert_emitted_exactly(records[at], step->record) && tier1_cert_emitted_append_move(build, at, BUSTER_IR_NONE, BUSTER_CERT_NONE);
        at += 1;
    }
    else if (valid && step->kind == TIER1_CERT_STEP_HIDDEN)
    {
        expected[expected_count++] = step->record;
    }
    else if (valid && step->kind == TIER1_CERT_STEP_PARAMETER)
    {
        // A parameter's part from its register into its copy: the part's value at entry, in its register.
        expected[expected_count++] = step->record;
        const uint32_t location = step->vector ? tier1_cert_emitted_vector(build, step->reg, step->width) : tier1_cert_emitted_gpr(step->reg, 8);
        valid = tier1_cert_emitted_add_use(&kernel, step->pseudo, location);
    }
    else if (valid && step->kind == TIER1_CERT_STEP_STORE)
    {
        // A result part into the result object: a vector register's whole, or its float's or double's, by its width's
        // store; its other bytes through the scratch register; a general register's (tier1_emit_call's).
        const uint8_t base = build->base;
        if (step->vector && (step->bytes == 4 || step->bytes == 8 || step->bytes >= 16))
        {
            const uint16_t form = step->bytes >= 16 ? tier1_select_part_form(step->bytes, false) : step->bytes == 8 ? X86_FORM_MOVSD_XM64_X : X86_FORM_MOVSS_XM32_X;
            expected[expected_count++] = (X86Instruction){ .displacement = step->to, .reg = step->reg, .rm = base, .mode = BUSTER_X86_MEMORY_BASE, .form = form };
        }
        else if (step->vector)
        {
            expected[expected_count++] = (X86Instruction){ .form = X86_FORM_MOVQ_RM64_X, .reg = step->reg, .rm = BUSTER_TIER1_SELECT_SCRATCH, .mode = BUSTER_X86_REGISTER_DIRECT };
            const uint32_t parts = tier1_cert_emitted_store_part(BUSTER_TIER1_SELECT_SCRATCH, step->bytes, base, step->to, expected + expected_count);
            valid = parts != 0;
            expected_count += parts;
        }
        else
        {
            const uint32_t parts = tier1_cert_emitted_store_part(step->reg, step->bytes, base, step->to, expected);
            valid = parts != 0;
            expected_count = parts;
        }
        const uint32_t location = step->vector ? tier1_cert_emitted_vector(build, step->reg, step->width) : tier1_cert_emitted_gpr(step->reg, 8);
        valid = valid && tier1_cert_emitted_add_use(&kernel, step->pseudo, location);
    }
    // An ORIGINAL of the expected records: its uses read, what they write killed; then the value it defines.
    for (uint32_t k = 0; valid && k < expected_count; k += 1)
    {
        valid = at + k < end && tier1_cert_emitted_exactly(records[at + k], expected[k]) && tier1_cert_emitted_take_reference(build, at + k, 0);
    }
    valid = valid && (expected_count == 0 || tier1_cert_emitted_originals(build, node, &kernel, records + at, expected_count));
    at += valid ? expected_count : 0;
    if (valid && step->kind == TIER1_CERT_STEP_ZMM)
    {
        valid = tier1_cert_emitted_define(build, node, step->pseudo, tier1_cert_emitted_vector(build, step->reg, 64));
    }
    else if (valid && step->kind == TIER1_CERT_STEP_HIDDEN)
    {
        valid = tier1_cert_emitted_define(build, node, step->pseudo, tier1_cert_emitted_gpr(X86_REGISTER_RDI, 8));
    }
    *r = at;
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, *r);
}

// The values of steps `steps` (their widths and classes) into `values`: a whole copy's 64 bytes at a time.
static void tier1_cert_emitted_step_values(const Tier1CertEmittedStep* steps, uint32_t count, Tier1CertValue* values)
{
    for (uint32_t k = 0; k < count; k += 1)
    {
        const Tier1CertEmittedStep step = steps[k];
        for (uint32_t j = 0; step.kind != TIER1_CERT_STEP_LOAD && step.kind != TIER1_CERT_STEP_SLOT && j < (step.chunks != 0 ? step.chunks : 1); j += 1)
        {
            const uint32_t left = step.size - BUSTER_CERT_MAX_WIDTH * j;
            values[step.pseudo + j] = (Tier1CertValue){
                .width = (uint16_t)(step.chunks != 0 ? (left < BUSTER_CERT_MAX_WIDTH ? left : BUSTER_CERT_MAX_WIDTH) : step.width),
                .class = step.vector ? TIER1_CERT_VECTOR : TIER1_CERT_GPR,
            };
        }
    }
}

// Call node `node`'s whole aggregates' bytes in the outgoing area (#789, #1284), 64 bytes at a time, each read where its
// copy put it: an ORIGINAL of their own right before the call's, as many as they are.
static bool tier1_cert_emitted_wholes(Tier1CertEmittedBuild* build, uint32_t node)
{
    const Tier1CertEmittedStep* steps = build->call_steps[node];
    const uint32_t step_count = build->call_step_count[node];
    bool valid = true;
    for (uint32_t k = 0; valid && k < step_count; k += 1)
    {
        Tier1CertEmittedKernel kernel = {
            .referenced = UINT32_MAX,
            .definition = BUSTER_TIER1_CFG_NONE,
            .definition_location = BUSTER_CERT_NONE,
        };
        for (uint32_t j = 0; valid && j < steps[k].chunks; j += 1)
        {
            const uint32_t left = steps[k].size - BUSTER_CERT_MAX_WIDTH * j;
            const uint32_t width = left < BUSTER_CERT_MAX_WIDTH ? left : BUSTER_CERT_MAX_WIDTH;
            valid = tier1_cert_emitted_add_use(&kernel, steps[k].pseudo + j, tier1_cert_emitted_stack(build, (int64_t)steps[k].to + BUSTER_CERT_MAX_WIDTH * j, width));
        }
        valid = valid && (steps[k].chunks == 0 || tier1_cert_emitted_originals(build, node, &kernel, 0, 0));
    }
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, UINT32_MAX);
}

// A call's expected sequence (tier1_emit_call's, for a call without aggregates or stack arguments): a variadic call's
// al, the call, and its result into its register, extended to its canonical form; its register arguments read in their
// ABI registers (a static chain's in r10, #148), an indirect call's callee in r10, or with a chain in rax
// (tier1_select_callee_register).
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
        // A by-value aggregate's (#789) is its parts', below.
        const bool aggregate = place.kind == BUSTER_TIER1_PLACE_AGGREGATE;
        valid = value != BUSTER_TIER1_CFG_NONE && (place.kind == BUSTER_TIER1_PLACE_GENERAL || place.kind == BUSTER_TIER1_PLACE_VECTOR || place.kind == BUSTER_TIER1_PLACE_STACK || aggregate);
        const uint32_t where = place.kind == BUSTER_TIER1_PLACE_GENERAL ? 32 * BUSTER_TIER1_ALLOCATE_GPR + tier1_select_parameter_registers[place.index] : 32 * BUSTER_TIER1_ALLOCATE_VECTOR + place.index;
        // An argument past its class's registers is read from its 8-byte slot in the outgoing area, at rsp: its own bytes.
        const uint32_t location = place.kind == BUSTER_TIER1_PLACE_STACK ? tier1_cert_emitted_frame(build, (int64_t)BUSTER_ABI_SLOT_BYTES * place.index, tier1_cert_emitted_width(emitted, value)) : tier1_cert_emitted_register(build, value, where);
        valid = valid && (aggregate || tier1_cert_emitted_add_use(kernel, value, location));
        vectors += place.kind == BUSTER_TIER1_PLACE_VECTOR;
    }
    // The by-value aggregates' parts (#789, #1284), each read where its steps leave it: a register part in its argument
    // register, one in a stack slot in its slot, a whole aggregate's bytes in its slots; the hidden pointer in rdi. And the
    // result parts, which the call defines in their registers.
    const Tier1CertEmittedStep* steps = build->call_steps[node];
    const uint32_t step_count = build->call_step_count[node];
    for (uint32_t k = 0; valid && k < step_count; k += 1)
    {
        const Tier1CertEmittedStep step = steps[k];
        if (step.kind == TIER1_CERT_STEP_COPY && step.slot && step.chunks == 0)
        {
            valid = tier1_cert_emitted_add_use(kernel, step.pseudo, tier1_cert_emitted_stack(build, step.to, step.width));
        }
        const uint32_t location = step.vector ? tier1_cert_emitted_vector(build, step.reg, step.width) : tier1_cert_emitted_gpr(step.reg, 8);
        if (valid && (step.kind == TIER1_CERT_STEP_LOAD || step.kind == TIER1_CERT_STEP_HIDDEN))
        {
            valid = tier1_cert_emitted_add_use(kernel, step.pseudo, location);
        }
        else if (valid && step.kind == TIER1_CERT_STEP_STORE)
        {
            valid = kernel->result_count < BUSTER_ARRAY_LENGTH(kernel->results) && location != BUSTER_CERT_NONE;
            kernel->results[valid ? kernel->result_count : 0] = step.pseudo;
            kernel->result_locations[valid ? kernel->result_count : 0] = location;
            kernel->result_count += valid;
        }
    }
    if (valid && indirect)
    {
        const uint32_t value = emitted->node_values[build->plan->effective_use_values[offset + call->argument_count]];
        valid = value != BUSTER_TIER1_CFG_NONE && tier1_cert_emitted_add_use(kernel, value, tier1_cert_emitted_register(build, value, tier1_select_callee_register(function, call)));
    }
    const uint32_t vector_count = abi != 0 ? abi->call.vector_registers : vectors;
    kernel->outgoing = tier1_select_call_stack_bytes(function, call);
    kernel->call = true;
    if (valid && call->variadic)
    {
        kernel->records[kernel->count++] = vector_count == 0 ? (X86Instruction){ .form = X86_FORM_XOR_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX }
                                                             : (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RAX, .immediate = (int32_t)vector_count };
    }
    if (valid && indirect)
    {
        kernel->records[kernel->count++] = (X86Instruction){ .form = X86_FORM_CALL_RM64, .rm = tier1_select_callee_register(function, call) };
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
    // The result, by its type: a floating one from xmm0, a narrow one zero-extended from al or ax, any other moved where it
    // isn't in rax. An I32 in rax has no move: its bits past its 32 are unspecified in Tier 1 (#1116), as above.
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
            else if (valid && reg != X86_REGISTER_RAX)
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
        .definition_location = defines && destination != UINT32_MAX ? tier1_cert_emitted_register(build, value, destination) : BUSTER_CERT_NONE,
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
        for (uint32_t p = 0; valid && p < BUSTER_TIER1_SELECT_VALUES; p += 1)
        {
            const uint32_t operand = pattern->values[p];
            const uint32_t operand_value = operand != BUSTER_IR_NONE ? emitted->node_values[operand] : BUSTER_TIER1_CFG_NONE;
            const uint32_t where = operand != BUSTER_IR_NONE ? tier1_cert_emitted_use(emitted, operand_value, use) : 0;
            valid = operand == BUSTER_IR_NONE || (where != UINT32_MAX && tier1_cert_emitted_add_use(kernel, operand_value, tier1_cert_emitted_register(build, operand_value, where)));
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
        else if (valid && pattern->rule == TIER1_RULE_VECTOR && pattern->scale == BUSTER_TIER1_VECTOR_CONSTANT)
        {
            // A vector constant of no simpler kind (#141, #1284): its words stored into the red zone, then the vector loaded
            // from there (tier1_emit_cfg_vector_constant), which the frame keeps its rows out of.
            Tier1EmitBuffer buffer = {
                .records = kernel->records,
                .capacity = BUSTER_ARRAY_LENGTH(kernel->records),
            };
            valid = tier1_emit_cfg_vector_constant(function, &holes, &buffer) == TIER1_EMIT_OK;
            kernel->count = buffer.count;
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
            kernel->referenced = referenced ? kernel->reference.instruction : UINT32_MAX;
        }
        for (uint32_t p = 0; p < BUSTER_TIER1_SELECT_VALUES; p += 1)
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

// The frame bytes a node's records write (tier1_cert_emitted_originals), as spans of the frame base register's
// displacements or (`entry`) of offsets from rsp at entry, each merged into the last where it touches it: a copy's stores
// of its pieces one after another make one.
#define BUSTER_CERT_EMITTED_SPANS 32u
typedef struct Tier1CertEmittedSpans Tier1CertEmittedSpans;
struct Tier1CertEmittedSpans
{
    int64_t starts[BUSTER_CERT_EMITTED_SPANS];
    int64_t ends[BUSTER_CERT_EMITTED_SPANS];
    bool entries[BUSTER_CERT_EMITTED_SPANS];
    uint32_t count;
};

static bool tier1_cert_emitted_span(Tier1CertEmittedSpans* spans, int64_t start, int64_t end, bool entry)
{
    const uint32_t last = spans->count - 1;
    const bool merged = spans->count != 0 && spans->entries[last] == entry && start <= spans->ends[last] && end >= spans->starts[last];
    const bool room = merged || spans->count < BUSTER_CERT_EMITTED_SPANS;
    if (merged)
    {
        spans->starts[last] = start < spans->starts[last] ? start : spans->starts[last];
        spans->ends[last] = end > spans->ends[last] ? end : spans->ends[last];
    }
    else if (room)
    {
        spans->starts[spans->count] = start;
        spans->ends[spans->count] = end;
        spans->entries[spans->count] = entry;
        spans->count += 1;
    }
    return room;
}

// Span [start, end) merged into the last span of its range (`entry`), widening it over the bytes between them: where
// the spans have no room left.
static bool tier1_cert_emitted_widen(Tier1CertEmittedSpans* spans, int64_t start, int64_t end, bool entry)
{
    uint32_t last = spans->count;
    for (uint32_t k = 0; k < spans->count; k += 1)
    {
        last = spans->entries[k] == entry ? k : last;
    }
    if (last < spans->count)
    {
        spans->starts[last] = start < spans->starts[last] ? start : spans->starts[last];
        spans->ends[last] = end > spans->ends[last] ? end : spans->ends[last];
    }
    return last < spans->count;
}

// The spans' frame storage, as locations of 64 bytes at most onto `clobbers` (`capacity` of them past `*count` at
// most): each span's bytes that are in its range of units, an entry offset's the frame base register's where the frame
// has their distance (tier1_cert_emitted_entry_frame).
static bool tier1_cert_emitted_kills(Tier1CertEmittedBuild* build, const Tier1CertEmittedSpans* spans, uint32_t* clobbers, uint32_t* count, uint32_t capacity)
{
    const uint32_t first = *count;
    bool valid = true;
    for (uint32_t k = 0; valid && k < spans->count; k += 1)
    {
        const bool entry = spans->entries[k] && build->realigned;
        // An entry offset in a frame whose base register's distance from the entry is known: that register's displacement.
        const int64_t shift = spans->entries[k] && !build->realigned ? build->frame_base : 0;
        valid = shift != INT64_MIN;
        const int64_t start = spans->starts[k] - shift;
        const int64_t end = spans->ends[k] - shift;
        const int64_t low = entry ? build->entry_low : build->low;
        const int64_t high = entry ? build->entry_high : build->high;
        const int64_t from = start > low ? start : low;
        const int64_t to = end < high ? end : high;
        for (int64_t at = from; valid && at < to; at += BUSTER_CERT_MAX_WIDTH)
        {
            const uint32_t width = (uint32_t)(to - at < BUSTER_CERT_MAX_WIDTH ? to - at : BUSTER_CERT_MAX_WIDTH);
            const uint32_t location = *count - first < capacity ? (entry ? tier1_cert_emitted_entry_frame(build, at, width) : tier1_cert_emitted_frame(build, at, width)) : BUSTER_CERT_NONE;
            valid = location != BUSTER_CERT_NONE;
            clobbers[*count] = location;
            *count += valid;
        }
    }
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX);
}

// Node `node`'s ORIGINALs, its records [first, first + count) its kernel's: its uses in eights, then every register and
// frame range its records write, killed in eights, then its definition. A general register its records give back is not
// killed: one that holds its own value at the run's end, followed through the records' full-width copies between general
// registers (a compare-exchange's save of rax in the scratch register and its restore, tier1_select's
// TIER1_RULE_ATOMIC_RMW); any other write leaves a register holding nothing known.
static bool tier1_cert_emitted_originals(Tier1CertEmittedBuild* build, uint32_t node, const Tier1CertEmittedKernel* kernel, const X86Instruction* records, uint32_t count)
{
    uint64_t writes = 0;
    uint16_t masks = 0;
    uint32_t clobbers[BUSTER_CERT_EMITTED_CLOBBERS];
    uint32_t clobber_count = 0;
    Tier1CertEmittedSpans spans;
    spans.count = 0;
    // Whose value at the run's start each general register holds, or UINT8_MAX for none known.
    uint8_t holds[16];
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        holds[reg] = (uint8_t)reg;
    }
    bool valid = true;
    for (uint32_t r = 0; valid && r < count; r += 1)
    {
        uint64_t read = 0;
        uint64_t written = 0;
        uint16_t opmasks = 0;
        const X86Instruction record = records[r];
        x86_instruction_effects_opmasks_scalar(&record, &read, &written, &opmasks);
        // An asm body's record of a form the semantics' tables don't cover (cpuid, the x87's, MMX's): its registers are
        // the body's touched ones, killed below, and of the rest it writes at most memory through its operand; the front
        // end placed no record of the stack's or control's in a body (check_asm_inline_touched).
        const bool unknown = kernel->body != 0 && x86_effects(record.form)->operation == X86_OPERATION_NONE;
        written = unknown ? ((record.mode & BUSTER_X86_MODE_MEMORY) != 0 ? BUSTER_X86_RESOURCE_MEMORY : 0) : written;
        opmasks = unknown ? 0 : opmasks;
        writes |= written;
        masks |= opmasks;
        // A copy is a 64-bit mov between general registers with no other field: its destination then holds what its
        // source held, after every other register the record writes holds nothing known.
        const bool plain = record.mode == BUSTER_X86_REGISTER_DIRECT && record.immediate == 0 && record.index == 0 && record.vvvv == 0 && record.decorations == 0 &&
                           record.displacement == 0 && record.reg < 16 && record.rm < 16;
        const bool load = plain && record.form == X86_FORM_MOV_R64_RM64;
        const bool store = plain && record.form == X86_FORM_MOV_RM64_R64;
        const uint8_t copied = load ? holds[record.rm] : store ? holds[record.reg] : UINT8_MAX;
        for (uint32_t reg = 0; reg < 16; reg += 1)
        {
            holds[reg] = (written & BUSTER_X86_RESOURCE_GPR(reg)) != 0 ? UINT8_MAX : holds[reg];
        }
        if (load || store)
        {
            holds[load ? record.reg : record.rm] = copied;
        }
        // No record writes the frame base register but the prologue and the epilogues, nor rsp but a call's and a ret's
        // own push and pop.
        const bool control = (written & (BUSTER_X86_RESOURCE_CALL | BUSTER_X86_RESOURCE_CONTROL)) != 0;
        valid = ((written & BUSTER_X86_RESOURCE_GPR(build->base)) == 0 || (build->base == X86_REGISTER_RSP && control)) && ((written & BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RSP)) == 0 || control);
        valid = valid && (build->frame_pointer == INT64_MIN || (written & BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RBP)) == 0);
        valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, UINT32_MAX);
        // A store into the frame kills the bytes it writes that are the frame's storage; a write the semantics doesn't
        // bound kills the 64 bytes from its operand on.
        int64_t at = 0;
        bool entry = false;
        if ((written & BUSTER_X86_RESOURCE_MEMORY) != 0 && tier1_cert_emitted_frame_operand(build, records[r], &at, &entry))
        {
            const X86FormDescription* description = x86_form_description(records[r].form & (BUSTER_X86_FORM_FIXUP - 1));
            const int64_t end = at + (description->operand_bytes != 0 ? description->operand_bytes : BUSTER_CERT_MAX_WIDTH);
            // An asm body's stores past the spans' room widen the last one of their range (more killed, never less).
            valid = valid && (tier1_cert_emitted_span(&spans, at, end, entry) || (kernel->body != 0 && tier1_cert_emitted_widen(&spans, at, end, entry)));
        }
    }
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        writes &= holds[reg] == reg ? ~BUSTER_X86_RESOURCE_GPR(reg) : ~UINT64_C(0);
    }
    // An asm body's touched registers (rax to r15 in bits 0 to 15, xmm0 to xmm15 in bits 16 to 31) and opmask ones.
    for (uint32_t reg = 0; kernel->body != 0 && reg < 16; reg += 1)
    {
        writes |= ((kernel->body->touched >> reg) & 1) != 0 ? BUSTER_X86_RESOURCE_GPR(reg) : 0;
        writes |= ((kernel->body->touched >> (16 + reg)) & 1) != 0 ? BUSTER_X86_RESOURCE_VECTOR(reg) : 0;
    }
    masks |= kernel->body != 0 ? (uint16_t)((uint16_t)kernel->body->masks << 8) : 0;
    // A call's callee may change every caller-saved register (the SysV ABI's: rax, rcx, rdx, rsi, rdi, r8 to r11, every
    // xmm and opmask register), which x86_semantics' effects of the call instruction itself don't say; and it owns its
    // outgoing area.
    if (kernel->call)
    {
        writes |= BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RAX) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RCX) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RDX) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RSI) |
                  BUSTER_X86_RESOURCE_GPR(X86_REGISTER_RDI) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R8) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R9) | BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R10) |
                  BUSTER_X86_RESOURCE_GPR(X86_REGISTER_R11) | BUSTER_X86_RESOURCE_VECTORS;
        masks |= 0xff00;
        // And the stack below rsp: the call's push of its return address, and the callee's frame, over the red zone's
        // staging slots (#789).
        int64_t at = 0;
        bool entry = false;
        valid = valid && (!tier1_cert_emitted_place(build, X86_REGISTER_RSP, -(int64_t)BUSTER_ABI_RED_ZONE_BYTES, &at, &entry) || tier1_cert_emitted_span(&spans, at, at + BUSTER_ABI_RED_ZONE_BYTES, entry));
    }
    valid = valid && (kernel->outgoing == 0 || tier1_cert_emitted_span(&spans, 0, kernel->outgoing, false));
    valid = valid && tier1_cert_emitted_kills(build, &spans, clobbers, &clobber_count, BUSTER_ARRAY_LENGTH(clobbers) - BUSTER_CERT_EMITTED_REGISTER_CLOBBERS);
    for (uint32_t reg = 0; valid && reg < 32; reg += 1)
    {
        if (reg < 16 && reg != X86_REGISTER_RSP && (writes & BUSTER_X86_RESOURCE_GPR(reg)) != 0)
        {
            clobbers[clobber_count++] = tier1_cert_emitted_gpr(reg, 8);
        }
        // A vector register a wide function's layout has (zmm16 and on its own: a machine row's, a part's staging, #1284).
        const uint32_t whole = tier1_cert_emitted_vector_whole(build, reg);
        if (whole != BUSTER_CERT_NONE && (writes & BUSTER_X86_RESOURCE_VECTOR(reg)) != 0)
        {
            clobbers[clobber_count++] = whole;
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

// Whether a by-value aggregate's parts at `location` of `size` bytes (#789) take a whole ymm or zmm register (#141).
static bool tier1_cert_emitted_wide_parts(AbiLocation location, uint32_t size)
{
    Tier1SelectPart parts[2];
    const uint32_t count = tier1_select_location_parts(location, size, parts);
    bool wide = false;
    for (uint32_t k = 0; k < count; k += 1)
    {
        wide |= parts[k].reg != BUSTER_ABI_REGISTER_NONE && (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR && parts[k].bytes > 16;
    }
    return wide;
}

// Whether the function needs the wide layout's vector registers (Tier1CertEmittedBuild.wide, #1284): a machine row, whose
// roles are zmm16 and on; a vector wider than 8 bytes; or a by-value aggregate's part in a whole ymm or zmm register, a
// parameter's, its result's or a call's, which zmm16 and on may stage.
static bool tier1_cert_emitted_wide(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    bool wide = function->row_count != 0;
    for (uint32_t v = 0; !wide && v < input->value_count; v += 1)
    {
        wide = input->classes[v] == BUSTER_TIER1_ALLOCATE_VECTOR && emitted->prepared.sizes[emitted->value_nodes[v]] > 8;
    }
    for (uint32_t p = 0; !wide && function->parameter_passed != 0 && p < function->parameter_count; p += 1)
    {
        const IrPassed* record = tier1_select_parameter_record(function, p);
        wide = record != 0 && record->size != 0 && tier1_cert_emitted_wide_parts(record->location, record->size);
    }
    Tier1SelectPart parts[BUSTER_TIER1_SELECT_RESULT_PARTS];
    const uint32_t returned = function->result_size != 0 ? tier1_select_result_parts(function->result_abi, function->result_size, parts) : 0;
    for (uint32_t k = 0; !wide && k < returned; k += 1)
    {
        wide = (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR && parts[k].bytes > 16;
    }
    for (uint32_t node = 0; !wide && node < function->instruction_count; node += 1)
    {
        const uint32_t op = function->op[node];
        const IrCall* call = (op == IR_CALL || op == IR_CALL_INDIRECT) && !plan->covered[node] ? &function->calls[function->b[node]] : 0;
        const IrAbiCall* abi = call != 0 && call->abi != 0 ? &function->abi_calls[call->abi - 1] : 0;
        const uint32_t results = abi != 0 && abi->result_size != 0 ? tier1_select_result_parts(abi->call.result, abi->result_size, parts) : 0;
        for (uint32_t k = 0; k < results; k += 1)
        {
            wide |= (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR && parts[k].bytes > 16;
        }
        for (uint32_t argument = 0; !wide && call != 0 && argument < call->argument_count; argument += 1)
        {
            const IrPassed* record = tier1_select_argument_record(function, call, argument);
            wide = record != 0 && record->size != 0 && tier1_cert_emitted_wide_parts(record->location, record->size);
        }
    }
    return wide;
}

// The function's shape that the decoding doesn't cover, by reason, or TIER1_CERT_EMITTED_OK.
static Tier1CertEmittedReason tier1_cert_emitted_supported(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const Tier1CfgInput* input = &emitted->cfg_input;
    Tier1CertEmittedReason reason = TIER1_CERT_EMITTED_OK;
    // A realigned frame is decoded (#1284), but one whose frame base register is the realignment's own (a dynamic stack's).
    if (emitted->frame.dynamic_outgoing || (emitted->frame.realigned && emitted->frame.base_register != X86_REGISTER_RSP))
    {
        reason = TIER1_CERT_EMITTED_DYNAMIC;
    }
    else if (emitted->frame.variadic)
    {
        reason = TIER1_CERT_EMITTED_VARIADIC;
    }
    else if (emitted->prepared.x87_count != 0)
    {
        reason = TIER1_CERT_EMITTED_X87;
    }
    // A by-value aggregate's parts are decoded (#789, #1284), but the x87's (a long double's in st0 and st1).
    else if (function->result_size != 0 && tier1_select_x87_parts(function->result_abi, function->result_size) != 0)
    {
        reason = TIER1_CERT_EMITTED_X87;
    }
    else if (input->predecessor_offsets[1] != input->predecessor_offsets[0])
    {
        reason = TIER1_CERT_EMITTED_ENTRY_LOOP;
    }
    // An asm body that moves a value into rsp (#1846, IrAsmBody.stack) leaves the frame to the program: a dynamic one.
    for (uint32_t b = 0; reason == TIER1_CERT_EMITTED_OK && b < function->asm_body_count && function->asm_bodies != 0; b += 1)
    {
        reason = function->asm_bodies[b].stack ? TIER1_CERT_EMITTED_DYNAMIC : reason;
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
        else if (op == IR_PARAM && tier1_select_entry_parameter(function, node) && tier1_select_parameter_place(function, function->a[node]).kind != BUSTER_TIER1_PLACE_GENERAL &&
                 tier1_select_parameter_place(function, function->a[node]).kind != BUSTER_TIER1_PLACE_VECTOR)
        {
            // A parameter on the stack, in the incoming area, which the frame base register must reach.
            const Tier1SelectPlace place = tier1_select_parameter_place(function, function->a[node]);
            uint8_t base = 0;
            int32_t offset = 0;
            const bool reached = place.kind == BUSTER_TIER1_PLACE_STACK && tier1_emit_cfg_incoming(emitted, (uint64_t)BUSTER_ABI_SLOT_BYTES * place.index, &base, &offset) &&
                                 (base == emitted->frame.base_register || (emitted->frame.frame_pointer && base == X86_REGISTER_RBP));
            reason = reached ? reason : place.kind == BUSTER_TIER1_PLACE_STACK ? TIER1_CERT_EMITTED_STACK : TIER1_CERT_EMITTED_AGGREGATE;
        }
        else if ((op == IR_CALL || op == IR_CALL_INDIRECT) && !plan->covered[node])
        {
            // A call's stack arguments go in its outgoing area from rsp, which must be the frame base register.
            const IrCall* call = &function->calls[function->b[node]];
            const IrAbiCall* abi = call->abi != 0 ? &function->abi_calls[call->abi - 1] : 0;
            Tier1SelectPlaces places = { 0 };
            const bool x87 = abi != 0 && abi->result_size != 0 && tier1_select_x87_parts(abi->call.result, abi->result_size) != 0;
            reason = tier1_select_call_stack_bytes(function, call) != 0 && emitted->frame.base_register != X86_REGISTER_RSP ? TIER1_CERT_EMITTED_STACK : x87 ? TIER1_CERT_EMITTED_X87 : reason;
            for (uint32_t argument = 0; reason == TIER1_CERT_EMITTED_OK && argument < call->argument_count; argument += 1)
            {
                const Tier1SelectPlace place = tier1_select_argument_place(function, call, argument, &places);
                reason = place.kind == BUSTER_TIER1_PLACE_GENERAL || place.kind == BUSTER_TIER1_PLACE_VECTOR || place.kind == BUSTER_TIER1_PLACE_STACK || place.kind == BUSTER_TIER1_PLACE_AGGREGATE ? reason : TIER1_CERT_EMITTED_AGGREGATE;
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

// Whether `record` writes any flag, which a conditional jump after it would read instead of its condition's.
static bool tier1_cert_emitted_writes_flags(const X86Instruction* record)
{
    uint64_t read = 0;
    uint64_t written = 0;
    x86_instruction_effects_scalar(record, &read, &written);
    return (written & BUSTER_X86_RESOURCE_FLAGS) != 0;
}

// Block `b`'s tail, walked for its edge `e` (`outcome` the condition's value for a conditional branch's, its form
// `condition`): from `start`, through its jumps and its edges' moves, to its successor's first record. True where it
// gets there through edge e's moves alone; `taken` counts them. A path may leave the block only by a jump or past its end,
// and arrives where it reaches the successor's first record having left `start`, or outside the tail. A conditional jump
// reads the condition only while `flags` (the body left them its comparison's) and no edge move before it wrote them.
static bool tier1_cert_emitted_walk(Tier1CertEmittedBuild* build, uint32_t b, uint32_t e, bool outcome, uint16_t condition, bool flags, uint32_t start, uint32_t* taken)
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
            // An edge's move that writes the flags (a zero's xor) leaves no condition for a conditional jump after it.
            valid = (origin & BUSTER_TIER1_ORIGIN_PAYLOAD) == e;
            flags = flags && !tier1_cert_emitted_writes_flags(&emitted->code.records[pc]);
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
            valid = branch != UINT32_MAX && (jump || (conditional && flags));
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
// sub, and a realigned frame's `and` last (#1284). Nothing else may be in it: a probe's loop is a shape left uncertified
// before this. It gives the frame base register's offset, rsp's after it (unknown after a realignment), and where each
// push put its register. `end` is the record after it.
static bool tier1_cert_emitted_prologue(Tier1CertEmittedBuild* build, uint32_t* end)
{
    const Tier1EmitResult* emitted = build->emitted;
    const uint32_t prologue = tier1_origin(TIER1_ORIGIN_FRAME, 0);
    int64_t sp = 0;
    bool valid = true;
    bool realigned = false;
    // After the allocation, the realignment alone: a push below it would be bytes the rows may share in a realigned frame,
    // whose pushes' slots and rows are units apart (Tier1CertEmittedBuild.entry_low).
    bool allocated = false;
    build->frame_pointer = INT64_MIN;
    for (uint32_t reg = 0; reg < 16; reg += 1)
    {
        build->saves[reg] = INT64_MIN;
    }
    uint32_t r = 0;
    while (valid && r < emitted->code.count && emitted->origins[r] == prologue)
    {
        const X86Instruction record = emitted->code.records[r];
        if (!allocated && record.form == X86_FORM_PUSH_R64 && record.rm < 16 && record.rm != X86_REGISTER_RSP && build->saves[record.rm] == INT64_MIN &&
            tier1_cert_emitted_exactly(record, (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = record.rm }))
        {
            sp -= 8;
            build->saves[record.rm] = sp;
        }
        else if (!allocated && build->frame_pointer == INT64_MIN && tier1_cert_emitted_exactly(record, (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RSP, .rm = X86_REGISTER_RBP }))
        {
            build->frame_pointer = sp;
        }
        else if (!allocated && !realigned && tier1_cert_emitted_immediate_form(record.form, X86_FORM_SUB_RM64_IMM8, X86_FORM_SUB_RM64_IMM32, record.immediate) && record.immediate > 0 &&
                 tier1_cert_emitted_exactly(record, (X86Instruction){ .form = record.form, .rm = X86_REGISTER_RSP, .immediate = record.immediate }))
        {
            sp -= record.immediate;
            allocated = true;
        }
        else if (!realigned && emitted->frame.realigned && record.immediate == -(int64_t)emitted->frame.alignment &&
                 tier1_cert_emitted_immediate_form(record.form, X86_FORM_AND_RM64_IMM8, X86_FORM_AND_RM64_IMM32, record.immediate) &&
                 tier1_cert_emitted_exactly(record, (X86Instruction){ .form = record.form, .rm = X86_REGISTER_RSP, .immediate = record.immediate }))
        {
            // A realigned frame's `and rsp, -alignment` (#1284), its prologue's last record: rsp moves down to a multiple of
            // the alignment, by a distance that depends on rsp at entry, and the frame base register is rsp there.
            realigned = true;
            allocated = true;
        }
        else
        {
            valid = tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r);
        }
        r += 1;
    }
    build->realigned = realigned;
    build->stack = realigned ? INT64_MIN : sp;
    // The frame base register is rsp after the prologue, or the frame pointer it set; a realigned frame's rsp has no known
    // distance from the entry, and its rows have no offsets from it (tier1_cert_emitted_entry_frame).
    build->frame_base = build->base == X86_REGISTER_RSP ? build->stack : build->base == X86_REGISTER_RBP ? build->frame_pointer : INT64_MIN;
    valid = valid && ((build->frame_base != INT64_MIN || (realigned && build->base == X86_REGISTER_RSP)) || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r));
    // A realigned frame is realigned exactly by its layout's alignment, past 16 bytes, its frame pointer the protocol's
    // way back to the pushes.
    const uint32_t alignment = emitted->frame.alignment;
    const bool aligned = realigned == emitted->frame.realigned && (!realigned || (alignment > 16 && (alignment & (alignment - 1)) == 0 && emitted->frame.frame_pointer));
    valid = valid && (aligned || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r));
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
    // A frame pointer is the ABI's frame record: rbp pushed first and set right after, so that the caller's rbp is at it
    // and the return address above it, which the layout's offsets from rbp (tier1_emit_cfg_incoming's) and an unwinder
    // by frame pointers take.
    valid = valid && (build->frame_pointer == INT64_MIN || (build->frame_pointer == -8 && build->saves[X86_REGISTER_RBP] == -8) || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r));
    // A realigned frame's rsp is a multiple of its alignment, of 16 among them.
    const bool calls = emitted->prepared.call_count != 0;
    valid = valid && (!calls || realigned || ((sp % 16) + 16) % 16 == 8 || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_FRAME, r));
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
        const uint32_t slot = tier1_cert_emitted_entry_frame(build, build->sp, 8);
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
    else if (valid && (record.form == X86_FORM_AND_RM64_IMM8 || record.form == X86_FORM_AND_RM64_IMM32))
    {
        // A realignment (#1284): rsp is the frame base register's, at no known distance from the entry.
        build->sp = INT64_MIN;
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
        // A realigned frame's rsp is at no known distance from the entry until its reset from the frame pointer (#1284).
        const bool known = build->sp != INT64_MIN;
        if (valid && known && tier1_cert_emitted_immediate_form(record.form, X86_FORM_ADD_RM64_IMM8, X86_FORM_ADD_RM64_IMM32, record.immediate) && record.immediate > 0 &&
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
        else if (valid && known && record.form == X86_FORM_POP_R64 && record.rm < 16 && record.rm != X86_REGISTER_RSP && tier1_cert_emitted_exactly(record, (X86Instruction){ .form = X86_FORM_POP_R64, .rm = record.rm }))
        {
            const uint32_t slot = tier1_cert_emitted_entry_frame(build, build->sp, 8);
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

// ---------------------------------------------------------------------------------------------------------------------
// Machine rows (#141, #1284)
//
// A machine row's node (TIER1_RULE_ROW, tier1_emit_cfg_row) is its operands' moves into place and the borrowed opmask
// registers' saves (edits, at its definition phase), then the row's records, then its result's move and the borrowed
// registers' reloads (edits). The row's records are its template's (the IR's row, trusted as its selection: what its records
// compute from the registers its roles are given), placed by the consumer's claimed placement (Tier1RowPlacement,
// untrusted): the certificate places them by it on its own (tier1_cert_emitted_row_place) and compares them with the
// emitted ones, and they are one ORIGINAL that reads each operand at its role's register, kills every register and frame
// byte the records write (by x86_semantics' effects), and defines the result at its role's. The placement must give the
// roles registers no two of them share, but the result and its class's first operand (Tier 0's placement has them one
// register, and a row may read either as the other) and operands its records don't write (whose register the records only
// read); and no register a record reads or writes implicitly. Then a role's register is any, as a template's semantics
// doesn't depend on its registers' numbers. The checker proves the rest: that each operand is where the row reads it, after
// its move into place, and that no live value's register is written but where it is saved and restored.

// A role of a row's (tier1_cert_emitted_row_roles): its register file (0 general, 1 vector, 2 opmask), its register, and
// whether it is operand `operand`'s (UINT32_MAX for none) or the result's.
// A machine row's role (#1284): its operand (UINT32_MAX for none), its register file (general, vector, opmask) and
// register, whether it is the result's; and a VECTOR(j) role's j (UINT8_MAX for another role), and the records over
// which it holds its register (#1283), [start, end] by index plus one: from the row's entry (0) for an operand there, else
// from its first write; to the last record naming it, or past the row for the result. VECTOR(0)'s early register
// (`early`) holds the first operand up to the result's first write, which reads it alone.
typedef struct Tier1CertEmittedRole Tier1CertEmittedRole;
struct Tier1CertEmittedRole
{
    uint32_t operand;
    uint8_t file;
    uint8_t reg;
    bool result;
    uint8_t vector;
    uint8_t start;
    uint8_t end;
    bool early;
};

// Whether template record `record` writes VECTOR(j) in any field (tier1_select_row_vectors' reading of its fields: a form
// x86 semantics' tables don't cover writes every field it names).
static bool tier1_cert_emitted_row_writes(X86Instruction record, uint32_t j)
{
    const Tier0Fields fields = tier0_operation_fields(&record);
    const X86Effects* effects = x86_effects(record.form);
    const bool covered = effects->operation != X86_OPERATION_NONE;
    bool writes = false;
    for (uint32_t k = 0; k < fields.count; k += 1)
    {
        const uint8_t role = *fields.bytes[k];
        const bool vector = tier0_vector_register_class(fields.classes[k]) && role >= BUSTER_TIER0_ROLE && role < BUSTER_TIER0_ROLE_FLOATING(0);
        const uint32_t position = fields.bytes[k] == &record.reg ? BUSTER_X86_ROLE_REG : fields.bytes[k] == &record.vvvv ? BUSTER_X86_ROLE_VVVV : BUSTER_X86_ROLE_RM;
        const bool address = record.mode != BUSTER_X86_REGISTER_DIRECT && (fields.bytes[k] == &record.rm || fields.bytes[k] == &record.index);
        const uint32_t bits = covered ? x86_effects_role(effects, position) : BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE;
        writes |= vector && (role & 15u) == j && !address && (bits & BUSTER_X86_ROLE_WRITE) != 0;
    }
    return writes;
}

// Whether VECTOR role `a` is done with the register it shares with VECTOR role `b` before `b` takes it (#1283): `a` named
// last before `b`'s first write, or in that record where the write is a pure one (it reads none of `b`'s earlier value)
// of a form whose destination may be one of its sources, and that record doesn't write `a`: an instruction reads its
// sources before it writes its destination.
static bool tier1_cert_emitted_row_before(const Tier0Operation* row, const Tier1SelectRowVectors* facts, Tier1CertEmittedRole a, Tier1CertEmittedRole b)
{
    const bool born = !b.early && b.start != 0 && b.start == facts->first_write[b.vector & 15];
    const bool touching = born && a.end == b.start && ((facts->pure >> (b.vector & 15)) & 1) != 0 && ((facts->exclusive >> (b.start - 1)) & 1) == 0 &&
                          (a.early || !tier1_cert_emitted_row_writes(row->records[b.start - 1], a.vector));
    return (born && a.end < b.start) || touching;
}

// Whether VECTOR role `a`, which holds the same value as VECTOR role `b` in the register they share (#1871: one value as
// two operands, as _mm_packus_epi32(x, x) has it), is done reading it when `b` is first written: `b` never is, or `a` is
// named last before that record, or in it where the record's destination may be one of its sources (it reads them
// before it writes). Neither is an early role (#1283).
static bool tier1_cert_emitted_row_reread(const Tier1SelectRowVectors* facts, Tier1CertEmittedRole a, Tier1CertEmittedRole b)
{
    const uint32_t write = facts->first_write[b.vector & 15];
    const bool written = ((facts->written >> (b.vector & 15)) & 1) != 0;
    const bool read_first = write != 0 && (a.end < write || (a.end == write && ((facts->exclusive >> (write - 1)) & 1) == 0));
    return !a.early && !b.early && (!written || read_first);
}

#define BUSTER_CERT_EMITTED_ROLES (BUSTER_TIER0_OPERATION_VECTORS + 2 * BUSTER_TIER0_OPERATION_OPERANDS + 2 * BUSTER_TIER0_SCRATCH + 3 + 8)

// A role byte's register by the placement, in a field of register class `class`, or UINT8_MAX for no register: a vector
// field's VECTOR(j), FLOATING(j) or FLOATING_RESULT; a general one's INTEGER(j), INTEGER_RESULT or SCRATCH(k); an opmask
// one's MASK(j), MASK_RESULT or MASK_SCRATCH(k), or a number, renamed. `*file` its register file, `*named` the role's index
// among Tier1CertEmittedRole's (tier1_cert_emitted_row_roles').
static uint8_t tier1_cert_emitted_row_register(uint8_t role, uint32_t class, const Tier1RowPlacement* placement, uint32_t* named)
{
    uint8_t reg = UINT8_MAX;
    *named = UINT32_MAX;
    if (class == BUSTER_X86_CLASS_XMM || class == BUSTER_X86_CLASS_YMM || class == BUSTER_X86_CLASS_ZMM)
    {
        const bool vector = role >= BUSTER_TIER0_ROLE && role < BUSTER_TIER0_ROLE_FLOATING(0);
        const bool result = role == BUSTER_TIER0_ROLE_FLOATING_RESULT;
        const bool floating = role >= BUSTER_TIER0_ROLE_FLOATING(0) && role < BUSTER_TIER0_ROLE_FLOATING(BUSTER_TIER0_OPERATION_OPERANDS);
        reg = vector ? placement->vectors[role & 15] : result ? placement->floating_result : floating ? placement->operands[role & 7] : UINT8_MAX;
        *named = vector ? (uint32_t)(role & 15) : result ? 2 * BUSTER_TIER0_OPERATION_VECTORS : floating ? BUSTER_TIER0_OPERATION_VECTORS + (role & 7) : UINT32_MAX;
    }
    else if (class == BUSTER_X86_CLASS_GPR8 || class == BUSTER_X86_CLASS_GPR16 || class == BUSTER_X86_CLASS_GPR32 || class == BUSTER_X86_CLASS_GPR64)
    {
        const bool result = role == BUSTER_TIER0_ROLE_INTEGER_RESULT;
        const bool scratch = role >= BUSTER_TIER0_ROLE_SCRATCH(0) && role < BUSTER_TIER0_ROLE_SCRATCH(BUSTER_TIER0_SCRATCH);
        const bool integer = role >= BUSTER_TIER0_ROLE_INTEGER(0) && role < BUSTER_TIER0_ROLE_INTEGER(BUSTER_TIER0_OPERATION_OPERANDS);
        reg = result ? placement->integer_result : scratch ? placement->scratch[role & 3] : integer ? placement->operands[role & 7] : UINT8_MAX;
        *named = result ? 2 * BUSTER_TIER0_OPERATION_VECTORS + 1 : scratch ? 2 * BUSTER_TIER0_OPERATION_VECTORS + 2 + (role & 3) : integer ? BUSTER_TIER0_OPERATION_VECTORS + (role & 7) : UINT32_MAX;
    }
    else if (class == BUSTER_X86_CLASS_OPMASK)
    {
        const bool number = role < 8;
        const bool result = role == BUSTER_TIER0_ROLE_MASK_RESULT;
        const bool scratch = role >= BUSTER_TIER0_ROLE_MASK_SCRATCH(0) && role < BUSTER_TIER0_ROLE_MASK_SCRATCH(BUSTER_TIER0_SCRATCH);
        const bool mask = role >= BUSTER_TIER0_ROLE_MASK(0) && role < BUSTER_TIER0_ROLE_MASK(BUSTER_TIER0_OPERATION_OPERANDS);
        reg = number ? placement->renamed[role] : result ? placement->mask_result : scratch ? placement->mask_scratch[role & 3] : mask ? placement->masks[role & 7] : UINT8_MAX;
        *named = number ? 2 * BUSTER_TIER0_OPERATION_VECTORS + 6 + BUSTER_TIER0_SCRATCH + role : result ? 2 * BUSTER_TIER0_OPERATION_VECTORS + 6 + BUSTER_TIER0_SCRATCH + 8 : scratch ? 2 * BUSTER_TIER0_OPERATION_VECTORS + 6 + (role & 3) :
                 mask ? BUSTER_TIER0_OPERATION_VECTORS + BUSTER_TIER0_OPERATION_OPERANDS + (role & 7) : UINT32_MAX;
    }
    return reg;
}

// The names a row's roles take in tier1_cert_emitted_row_register's `*named`: VECTOR(j) at j, an operand's (INTEGER(j)'s or
// FLOATING(j)'s) at 16 + j, MASK(j)'s at 24 + j, the floating and integer results, SCRATCH(k), MASK_SCRATCH(k), the opmask
// registers by number and the mask result.
#define BUSTER_CERT_EMITTED_ROLE_NAMES (2 * BUSTER_TIER0_OPERATION_VECTORS + 6 + BUSTER_TIER0_SCRATCH + 9)

// Template record `record`, the row's `r`-th, placed by `placement` (#1284): each register field's role its register, an
// /is4 operand's VECTOR(j) in its immediate's bits 7 to 4, an opmask decoration's role its register and a number
// renamed. VECTOR(0) is `early` up to the result's first write, record `split` (#1283, tier1_emit_cfg_row_place): in the
// records before it, and in its fields but that write's (a VSIB index among them, which it reads). Each role it names into
// `named` (a bit per tier1_cert_emitted_row_register's name), and its explicit registers into `explicit_registers`
// (BUSTER_X86_RESOURCE_*) and `explicit_masks` (a bit per opmask register). False where a role has no register.
static bool tier1_cert_emitted_row_place(X86Instruction record, uint32_t r, const Tier1RowPlacement* placement, X86Instruction* placed, uint64_t* named, uint64_t* explicit_registers, uint32_t* explicit_masks)
{
    const X86FormDescription* description = x86_form_description(record.form);
    uint8_t* fields[4] = { 0 };
    uint32_t classes[4] = { 0 };
    uint32_t count = 0;
    X86Instruction result = record;
    const uint16_t operands[2] = { description->reg, description->vvvv };
    uint8_t* bytes[2] = { &result.reg, &result.vvvv };
    for (uint32_t k = 0; k < 2; k += 1)
    {
        if ((operands[k] & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_NONE)
        {
            fields[count] = bytes[k];
            classes[count] = operands[k] & BUSTER_X86_OPERAND_CLASS;
            count += 1;
        }
    }
    if (record.mode == BUSTER_X86_REGISTER_DIRECT && (description->rm & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_NONE)
    {
        fields[count] = &result.rm;
        classes[count] = description->rm & BUSTER_X86_OPERAND_CLASS;
        count += 1;
    }
    else if (record.mode != BUSTER_X86_REGISTER_DIRECT)
    {
        if ((record.mode & BUSTER_X86_MODE_BASE) != 0)
        {
            fields[count] = &result.rm;
            classes[count] = BUSTER_X86_CLASS_GPR64;
            count += 1;
        }
        if ((record.mode & BUSTER_X86_MODE_INDEX) != 0)
        {
            fields[count] = &result.index;
            classes[count] = (description->rm & BUSTER_X86_OPERAND_VSIB) != 0 ? description->rm & BUSTER_X86_OPERAND_CLASS : BUSTER_X86_CLASS_GPR64;
            count += 1;
        }
    }
    bool valid = true;
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        uint32_t name = UINT32_MAX;
        const uint8_t role = *fields[k];
        const bool vector = classes[k] == BUSTER_X86_CLASS_XMM || classes[k] == BUSTER_X86_CLASS_YMM || classes[k] == BUSTER_X86_CLASS_ZMM;
        const uint32_t at = fields[k] == &result.reg ? BUSTER_X86_ROLE_REG : fields[k] == &result.vvvv ? BUSTER_X86_ROLE_VVVV : BUSTER_X86_ROLE_RM;
        const bool before = r + 1 < placement->split || (r + 1 == placement->split && (at != placement->position || fields[k] == &result.index));
        const bool early = vector && role >= BUSTER_TIER0_ROLE && role < BUSTER_TIER0_ROLE_FLOATING(0) && (role & 15) == 0 && before;
        const uint8_t reg = early ? placement->early : tier1_cert_emitted_row_register(role, classes[k], placement, &name);
        name = early ? 0 : name;
        const bool mask = classes[k] == BUSTER_X86_CLASS_OPMASK;
        valid = reg != UINT8_MAX && name < BUSTER_CERT_EMITTED_ROLE_NAMES;
        *fields[k] = reg;
        *named |= valid ? UINT64_C(1) << name : 0;
        *explicit_registers |= valid && !mask ? (vector ? BUSTER_X86_RESOURCE_VECTOR(reg & 31) : BUSTER_X86_RESOURCE_GPR(reg & 15)) : 0;
        // A pair's field (VP2INTERSECT's) names its register and the next.
        const uint32_t pair = mask && ((fields[k] == &result.reg && description->reg == BUSTER_X86_OPERAND_KP) || (fields[k] == &result.vvvv && description->vvvv == BUSTER_X86_OPERAND_KP)) ? 3u : 1u;
        *explicit_masks |= valid && mask ? pair << (reg & 7) : 0;
    }
    const uint8_t syntax = description->syntax;
    const bool is4 = syntax == X86_SYNTAX_REG_VVVV_RM_IS4 || syntax == X86_SYNTAX_REG_VVVV_IS4_RM || syntax == X86_SYNTAX_REG_VVVV_RM_IS4_IMM || syntax == X86_SYNTAX_REG_VVVV_IS4_RM_IMM;
    if (valid && is4)
    {
        const uint32_t j = ((uint32_t)record.immediate >> 4) & 15;
        const uint32_t reg = j == 0 && r + 1 <= placement->split ? placement->early : placement->vectors[j];
        valid = reg < 16;
        result.immediate = (int32_t)((reg << 4) | ((uint32_t)record.immediate & 15));
        *named |= UINT64_C(1) << j;
        *explicit_registers |= valid ? BUSTER_X86_RESOURCE_VECTOR(reg) : 0;
    }
    // The opmask decoration: MASK(j) or MASK_SCRATCH(k) by bits 3 and 4, else a number, renamed (k0 is no mask).
    const uint32_t decoration = record.decorations & BUSTER_TIER0_DECORATION_ROLE;
    const uint32_t index = record.decorations & BUSTER_X86_DECORATION_MASK;
    const uint8_t role = decoration == BUSTER_TIER0_DECORATION_MASK(0) ? (uint8_t)BUSTER_TIER0_ROLE_MASK(index) : decoration == BUSTER_TIER0_DECORATION_MASK_SCRATCH(0) ? (uint8_t)BUSTER_TIER0_ROLE_MASK_SCRATCH(index & 3) : (uint8_t)index;
    uint32_t name = UINT32_MAX;
    const uint8_t mask = valid ? tier1_cert_emitted_row_register(role, BUSTER_X86_CLASS_OPMASK, placement, &name) : UINT8_MAX;
    valid = valid && mask < 8 && ((decoration == 0 && index == 0) || mask != 0) && name < BUSTER_CERT_EMITTED_ROLE_NAMES;
    result.decorations = (uint8_t)((record.decorations & ~(BUSTER_TIER0_DECORATION_ROLE | BUSTER_X86_DECORATION_MASK)) | (valid ? mask : 0));
    *named |= valid && (index != 0 || decoration != 0) ? UINT64_C(1) << name : 0;
    *explicit_masks |= valid && mask != 0 ? 1u << mask : 0;
    *placed = result;
    return valid;
}

// Machine row node `node`'s records [r, end), its node's (tier1_emit_cfg_row's: the row's records, then its result's move
// and the borrowed opmask registers' reloads; none for a row of no records that needs no move after them): the row's
// records, placed by the claimed placement, one ORIGINAL; the rest edits. A placement whose roles share a register where the
// row's semantics doesn't let them is refused; a row the semantics' tables don't cover, or whose records read or write a
// register implicitly that a role has, is left uncertified.
static bool tier1_cert_emitted_row(Tier1CertEmittedBuild* build, uint32_t node, uint32_t r, uint32_t end)
{
    const IrFunction* function = build->plan->function;
    const Tier1EmitResult* emitted = build->emitted;
    const uint32_t index = (uint32_t)build->plan->patterns[node].immediate;
    const Tier0Operation* row = function->op[node] == IR_CALL && index < function->row_count ? &function->rows[index] : 0;
    const uint32_t first = build->plan->effective_use_offsets[node];
    const Tier1RowPlacement* placement = emitted->row_placements != 0 ? &emitted->row_placements[node] : 0;
    bool valid = row != 0 && placement != 0 && row->record_count <= BUSTER_TIER0_OPERATION_RECORDS && row->operand_count <= BUSTER_TIER0_OPERATION_OPERANDS &&
                 build->plan->effective_use_offsets[node + 1] - first == row->operand_count && end >= r && end - r >= row->record_count;
    X86Instruction placed[BUSTER_TIER0_OPERATION_RECORDS];
    uint64_t named = 0;
    uint64_t implicit = 0;
    uint32_t implicit_masks = 0;
    uint64_t writes = 0;
    uint32_t mask_writes = 0;
    bool covered = true;
    for (uint32_t k = 0; valid && k < row->record_count; k += 1)
    {
        uint64_t explicit_registers = 0;
        uint32_t explicit_masks = 0;
        valid = tier1_cert_emitted_row_place(row->records[k], k, placement, &placed[k], &named, &explicit_registers, &explicit_masks);
        valid = valid && tier1_cert_emitted_exactly(emitted->code.records[r + k], placed[k]) && x86_validate(&placed[k], 1) == 1 && tier1_cert_emitted_take_reference(build, r + k, 0);
        uint64_t read = 0;
        uint64_t written = 0;
        uint16_t opmasks = 0;
        x86_instruction_effects_opmasks_scalar(&placed[k], &read, &written, &opmasks);
        covered = covered && x86_effects(placed[k].form)->operation != X86_OPERATION_NONE;
        implicit |= (read | written) & (BUSTER_X86_RESOURCE_GPRS | BUSTER_X86_RESOURCE_VECTORS) & ~explicit_registers;
        implicit_masks |= ((uint32_t)opmasks | (uint32_t)opmasks >> 8) & 0xffu & ~explicit_masks;
        writes |= written;
        mask_writes |= (uint32_t)opmasks >> 8;
    }
    valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    // The roles: each operand's by its class, the result's, and each other role the records name; VECTOR(j)'s with the
    // records they hold their registers over, by the template's own uses of them (tier1_select_row_vectors).
    const Tier1SelectRowVectors facts = valid ? tier1_select_row_vectors(row) : (Tier1SelectRowVectors){ 0 };
    const uint8_t past = (uint8_t)(row != 0 ? row->record_count + 1 : 0);
    const bool split = valid && placement->split != 0;
    Tier1CertEmittedRole roles[BUSTER_CERT_EMITTED_ROLES];
    uint32_t role_count = 0;
    uint64_t taken = 0;
    // The first operand of each of Tier 0's stacks (tier0_stack): the integer one's (an integer's, a vector's or a memory
    // class's), the floating one's and the masks'.
    uint32_t first_of[3] = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
    for (uint32_t j = 0; valid && j < row->operand_count; j += 1)
    {
        const uint32_t class = row->operand_classes[j];
        const bool vector = tier0_class_vector(class);
        const bool floating = tier0_class_floating(class);
        const bool mask = tier0_class_mask(class);
        const uint8_t file = vector || floating ? 1 : mask ? 2 : 0;
        // The first operand read in its own register up to the result's first write (#1283).
        const bool early = vector && j == 0 && split;
        const uint8_t reg = early ? placement->early : vector ? placement->vectors[j] : mask ? placement->masks[j] : placement->operands[j];
        roles[role_count++] = (Tier1CertEmittedRole){
            .operand = j,
            .file = file,
            .reg = reg,
            .vector = vector ? (uint8_t)j : UINT8_MAX,
            .end = early ? placement->split : facts.last_named[j & 15],
            .early = early,
        };
        taken |= UINT64_C(1) << (vector ? j : mask ? 24 + j : 16 + j);
        const uint32_t stack = mask ? 2 : floating ? 1 : 0;
        first_of[stack] = first_of[stack] == UINT32_MAX ? j : first_of[stack];
    }
    const uint32_t result_class = row != 0 ? row->result_class : BUSTER_TYPED_CLASS_VOID;
    const bool vector_result = tier0_class_vector(result_class);
    const bool floating_result = tier0_class_floating(result_class);
    const bool mask_result = tier0_class_mask(result_class);
    const bool integer_result = tier0_class_integer(result_class);
    // The operand the result shares a register with, as Tier 0's placement has it (tier0_operation_placement,
    // tier0_operation_mask_place), which the row's records may take for granted: an integer result the first operand of
    // the integer stack where that is an integer (a vector's place there is no general register), a floating one the first
    // floating operand, a mask one the first mask operand; none else.
    const bool integer_alias = integer_result && first_of[0] != UINT32_MAX && tier0_class_integer(row->operand_classes[first_of[0]]);
    const uint32_t alias = integer_alias ? first_of[0] : floating_result ? first_of[1] : mask_result ? first_of[2] : UINT32_MAX;
    const uint8_t result_reg = placement == 0     ? UINT8_MAX
                               : vector_result   ? placement->vectors[0]
                               : floating_result ? placement->floating_result
                               : mask_result     ? placement->mask_result
                                                 : placement->integer_result;
    const bool first_vector = valid && row->operand_count != 0 && tier0_class_vector(row->operand_classes[0]);
    // A split (#1283) is VECTOR(0)'s first write, a pure one in the claimed field, the first operand a vector's.
    valid = valid && (!split || (vector_result && first_vector && placement->split == facts.first_write[0] && (facts.pure & 1) != 0 && placement->position == facts.write_position[0]));
    valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    if (valid && result_class != BUSTER_TYPED_CLASS_VOID)
    {
        // A vector result is VECTOR(0)'s, operand 0's own role where it is a vector read in VECTOR(0)'s register.
        const bool same = vector_result && first_vector && !split;
        roles[same ? 0 : role_count] = (Tier1CertEmittedRole){
            .operand = same ? 0 : UINT32_MAX,
            .file = (uint8_t)(vector_result || floating_result ? 1 : mask_result ? 2 : 0),
            .reg = result_reg,
            .result = true,
            .vector = vector_result ? 0 : UINT8_MAX,
            .start = vector_result && !same ? facts.first_write[0] : 0,
            .end = past,
        };
        role_count += same ? 0 : 1;
        taken |= UINT64_C(1) << (vector_result ? 0 : floating_result ? 32 : integer_result ? 33 : 50);
        // Tier 0's placement has the result in its class's first operand's register.
        valid = alias == UINT32_MAX || result_reg == (mask_result ? placement->masks[alias] : placement->operands[alias]);
        valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    }
    for (uint32_t name = 0; valid && name < BUSTER_CERT_EMITTED_ROLE_NAMES; name += 1)
    {
        // The other roles the records name: VECTOR(j), SCRATCH(k), MASK_SCRATCH(k) and the opmask registers by number.
        const bool other = ((named >> name) & 1) != 0 && ((taken >> name) & 1) == 0;
        const bool vector = name < BUSTER_TIER0_OPERATION_VECTORS;
        const bool scratch = name >= 34 && name < 34 + BUSTER_TIER0_SCRATCH;
        const bool mask_scratch = name >= 38 && name < 38 + BUSTER_TIER0_SCRATCH;
        const bool number = name >= 42 && name < 50;
        valid = !other || vector || scratch || mask_scratch || number;
        if (valid && other)
        {
            const uint8_t reg = vector ? placement->vectors[name] : scratch ? placement->scratch[name - 34] : mask_scratch ? placement->mask_scratch[name - 38] : placement->renamed[name - 42];
            valid = role_count < BUSTER_CERT_EMITTED_ROLES;
            roles[valid ? role_count : 0] = (Tier1CertEmittedRole){
                .operand = UINT32_MAX,
                .file = (uint8_t)(vector ? 1 : scratch ? 0 : 2),
                .reg = reg,
                .vector = vector ? (uint8_t)name : UINT8_MAX,
                .start = vector ? facts.first_write[name] : 0,
                .end = vector ? facts.last_named[name] : 0,
            };
            role_count += valid;
        }
        valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    }
    // Each role's register is one of its file's that holds a value: a general one but rsp, a vector one, an opmask one but
    // k0 (in a decoration, no mask at all), k0 by number only as itself and k1 to k7 by number only as one of them; a
    // pair's registers (VP2INTERSECT's) as they are.
    valid = valid && placement->renamed[0] == 0;
    for (uint32_t k = 1; valid && k < 8; k += 1)
    {
        valid = placement->renamed[k] != 0 && placement->renamed[k] < 8 && (!tier1_select_row_pairs(row) || placement->renamed[k] == k);
    }
    uint64_t role_registers = 0;
    uint32_t role_masks = 0;
    for (uint32_t k = 0; valid && k < role_count; k += 1)
    {
        const Tier1CertEmittedRole role = roles[k];
        valid = role.file == 0 ? role.reg < 16 && role.reg != X86_REGISTER_RSP : role.file == 1 ? role.reg < 32 : role.reg < 8;
        role_registers |= role.file == 0 ? BUSTER_X86_RESOURCE_GPR(role.reg & 15) : role.file == 1 ? BUSTER_X86_RESOURCE_VECTOR(role.reg & 31) : 0;
        role_masks |= role.file == 2 ? 1u << (role.reg & 7) : 0;
        // No two roles share a register while both hold their values, but where Tier 0's placement puts them together:
        // the result and its class's first operand, and operands no record writes. Two VECTOR(j) roles may share one
        // over records apart (#1283), one done with it before the other takes it, and two operands' roles of the same
        // value (#1871) where each is done reading it when the other is first written.
        for (uint32_t m = 0; valid && m < k; m += 1)
        {
            const Tier1CertEmittedRole other = roles[m];
            const bool shared = other.file == role.file && other.reg == role.reg;
            const bool aliased = alias != UINT32_MAX && ((role.result && other.operand == alias) || (other.result && role.operand == alias));
            const bool vectors = role.vector != UINT8_MAX && other.vector != UINT8_MAX;
            const bool unwritten = vectors ? (role.early || ((facts.written >> role.vector) & 1) == 0) && (other.early || ((facts.written >> other.vector) & 1) == 0)
                                   : role.file == 0 ? (writes & BUSTER_X86_RESOURCE_GPR(role.reg & 15)) == 0
                                   : role.file == 1 ? (writes & BUSTER_X86_RESOURCE_VECTOR(role.reg & 31)) == 0
                                                    : ((mask_writes >> (role.reg & 7)) & 1) == 0;
            const bool read_only = role.operand != UINT32_MAX && other.operand != UINT32_MAX && !role.result && !other.result && unwritten;
            const bool apart = vectors && (tier1_cert_emitted_row_before(row, &facts, role, other) || tier1_cert_emitted_row_before(row, &facts, other, role));
            const bool same_value = vectors && role.operand < row->operand_count && other.operand < row->operand_count &&
                                    build->plan->effective_use_values[first + role.operand] == build->plan->effective_use_values[first + other.operand];
            const bool reread = same_value && tier1_cert_emitted_row_reread(&facts, role, other) && tier1_cert_emitted_row_reread(&facts, other, role);
            valid = !shared || aliased || read_only || apart || reread;
        }
    }
    valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    // A form the tables don't cover, or a register a record names implicitly that a role has, isn't decoded.
    valid = valid && ((covered && (implicit & role_registers) == 0 && (implicit_masks & role_masks) == 0) || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_VECTOR, r));
    // One ORIGINAL: each operand read at its role's register, at its value's width, what the records write killed, the
    // result defined at its role's.
    Tier1CertEmittedKernel kernel = {
        .referenced = UINT32_MAX,
        .definition = BUSTER_TIER1_CFG_NONE,
        .definition_location = BUSTER_CERT_NONE,
    };
    for (uint32_t j = 0; valid && j < row->operand_count; j += 1)
    {
        const uint32_t class = row->operand_classes[j];
        const uint32_t value = emitted->node_values[build->plan->effective_use_values[first + j]];
        const uint32_t width = value != BUSTER_TIER1_CFG_NONE ? tier1_cert_emitted_width(emitted, value) : 0;
        const uint32_t location = tier0_class_vector(class)     ? tier1_cert_emitted_vector(build, roles[j].reg, width)
                                  : tier0_class_floating(class) ? tier1_cert_emitted_vector(build, placement->operands[j], width)
                                  : tier0_class_mask(class)     ? tier1_cert_emitted_mask(placement->masks[j], width)
                                                                : tier1_cert_emitted_gpr(placement->operands[j], width);
        valid = value != BUSTER_TIER1_CFG_NONE && tier1_cert_emitted_add_use(&kernel, value, location);
        valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    }
    const uint32_t value = result_class != BUSTER_TYPED_CLASS_VOID ? emitted->node_values[node] : BUSTER_TIER1_CFG_NONE;
    if (valid && value != BUSTER_TIER1_CFG_NONE)
    {
        const uint32_t width = tier1_cert_emitted_width(emitted, value);
        kernel.definition = value;
        kernel.definition_location = vector_result || floating_result ? tier1_cert_emitted_vector(build, result_reg, width) : mask_result ? tier1_cert_emitted_mask(result_reg, width) : tier1_cert_emitted_gpr(result_reg, width);
        valid = kernel.definition_location != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    }
    // A vector row of no records (a zero-record shuffle's: its result is its wider operand, in that operand's register)
    // writes nothing: its result's move (tier1_emit_cfg_row's, zero-extended for an SSE carrier) is its definition, at the
    // move's destination. Defining it in the operand's register would kill the operand, which may live past the node
    // (#1282's inlined intrinsics: two widenings of one vector).
    uint32_t viewed = 0;
    if (valid && value != BUSTER_TIER1_CFG_NONE && vector_result && row->record_count == 0 && end > r)
    {
        const X86Instruction move = emitted->code.records[r];
        const uint8_t destination = move.reg;
        const X86Instruction expected = row->result_bytes < 16 ? tier1_select_carrier_move(destination, result_reg, row->result_bytes, emitted->prepared.vex)
                                                               : tier1_select_register_record(tier1_select_vector_forms(row->result_bytes, tier1_emit_encoding(&emitted->prepared))->move, destination, result_reg);
        const uint32_t location = destination < 32 ? tier1_cert_emitted_vector(build, destination, tier1_cert_emitted_width(emitted, value)) : BUSTER_CERT_NONE;
        if (location != BUSTER_CERT_NONE && tier1_cert_emitted_exactly(move, expected))
        {
            valid = tier1_cert_emitted_take_reference(build, r, 0);
            kernel.definition_location = location;
            viewed = 1;
        }
    }
    valid = valid && tier1_cert_emitted_originals(build, node, &kernel, placed, row->record_count);
    // The result's move and the borrowed registers' reloads.
    for (uint32_t at = r + (valid ? row->record_count + viewed : 0); valid && at < end; at += 1)
    {
        valid = tier1_cert_emitted_append_move(build, at, BUSTER_IR_NONE, BUSTER_CERT_NONE);
    }
    return valid;
}

// An asm body's node (#1839, TIER1_RULE_ASM; tier1_emit_cfg_asm) is its records [r, end), between the saves and the
// restores of the values in the registers it touches, which are moves, decoded as any. The certificate places the body's
// records itself, from the IR's (the front end's template, trusted as a row's is): each that addresses the operand block
// (BUSTER_TIER0_ASM_BLOCK) addressing the block's frame object from the frame base register, by the frame's layout; and
// compares them with the emitted ones, which carry no reference. The body's own branches are exactly its branches, each
// to its target's record (its end's for one to the body's end), and no other branch leaves its records. The records
// are one ORIGINAL, of no use and no definition: they read the block's memory, which isn't a value the allocation keeps,
// and kill every register the body touches (IrAsmBody.touched and .masks: callee-saved ones too, which the prologue
// saved) and the frame bytes their stores write.
static bool tier1_cert_emitted_asm(Tier1CertEmittedBuild* build, uint32_t node, uint32_t r, uint32_t end)
{
    const IrFunction* function = build->plan->function;
    const Tier1EmitResult* emitted = build->emitted;
    const uint32_t index = (uint32_t)build->plan->patterns[node].immediate;
    const IrCall* call = function->op[node] == IR_CALL ? &function->calls[function->b[node]] : 0;
    const IrAsmBody* body = call != 0 && call->body != 0 && index < function->asm_body_count && call->body == index + 1 ? &function->asm_bodies[index] : 0;
    const uint32_t block = body != 0 && call->argument_count == 1 ? function->extra[call->argument_first] : BUSTER_IR_NONE;
    int32_t offset = 0;
    bool valid = body != 0 && end >= r && end - r == body->count &&
                 (block == BUSTER_IR_NONE || (block < function->instruction_count && function->op[block] == IR_STACK_ADDR && tier1_emit_cfg_object(function, emitted, function->a[block], &offset)));
    uint32_t branch = 0;
    for (uint32_t k = 0; valid && k < body->count; k += 1)
    {
        X86Instruction record = body->records[k];
        const bool addressed = (record.mode & BUSTER_X86_MODE_MEMORY) != 0 && (record.mode & BUSTER_X86_MODE_BASE) != 0 && record.rm == BUSTER_TIER0_ASM_BLOCK;
        valid = !addressed || block != BUSTER_IR_NONE;
        record.rm = addressed ? build->base : record.rm;
        record.displacement += addressed ? offset : 0;
        valid = valid && tier1_cert_emitted_exactly(emitted->code.records[r + k], record) && tier1_cert_emitted_take_reference(build, r + k, 0);
        // Its branch, exactly the body's, or none.
        const bool branches = branch < body->branch_count && body->branches[branch] == k;
        const uint32_t found = build->branch_of[r + k];
        valid = valid && (branches ? found != UINT32_MAX && body->targets[branch] <= body->count && emitted->code.targets[found] == r + body->targets[branch] : found == UINT32_MAX);
        branch += branches ? 1 : 0;
    }
    valid = valid && branch == body->branch_count;
    valid = valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r);
    Tier1CertEmittedKernel kernel = {
        .referenced = UINT32_MAX,
        .definition = BUSTER_TIER1_CFG_NONE,
        .definition_location = BUSTER_CERT_NONE,
        .condition = X86_FORM_NONE,
        .body = body,
    };
    // The emitted records, each now its placement's.
    return valid && tier1_cert_emitted_originals(build, node, &kernel, emitted->code.records + r, body->count);
}

// The function's own aggregate result's records [r, end) (#789, #1284; tier1_emit_cfg_aggregate_result's, before its
// return's epilogue), from the address `address` (a node) in general register `source`: in registers, each part's bytes
// copied into its staging slots, defining its value there (tier1_cert_emitted_result_part), then its register loaded from
// them; through the hidden pointer, the pointer loaded from its object into rax (r10 where the address is in rax, moved
// into rax after), and the bytes copied to it. Its ret reads them where it returns them (tier1_cert_emitted_return).
static bool tier1_cert_emitted_result(Tier1CertEmittedBuild* build, uint32_t node, uint32_t address, uint8_t source, uint32_t r, uint32_t end)
{
    const IrFunction* function = build->plan->function;
    const Tier1EmitResult* emitted = build->emitted;
    const X86Instruction* records = emitted->code.records;
    const AbiReturn result = function->result_abi;
    const uint32_t value = address < function->instruction_count ? emitted->node_values[address] : BUSTER_TIER1_CFG_NONE;
    Tier1SelectPart parts[BUSTER_TIER1_SELECT_RESULT_PARTS];
    const uint32_t part_count = tier1_select_result_parts(result, function->result_size, parts);
    const bool hidden = result.kind == BUSTER_ABI_RESULT_MEMORY;
    uint32_t at = r;
    // Every staged part's slots in the red zone (#1847, tier1_select_result_staged), as the consumer refuses any other.
    bool valid = source < 16 && source != BUSTER_TIER1_SELECT_SCRATCH && value != BUSTER_TIER1_CFG_NONE && r <= end && tier1_select_result_staged(parts, part_count);
    for (uint32_t k = 0; valid && k < part_count; k += 1)
    {
        if (tier1_select_part_direct(parts[k]))
        {
            continue;
        }
        const int32_t staged = tier1_emit_staging_part(tier1_select_result_slot(parts, k), parts[k].bytes);
        const uint32_t pieces = tier1_cert_emitted_pieces(parts[k].bytes);
        const bool vector = (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        valid = end - at >= 2 * pieces;
        for (uint32_t j = 0; valid && j < pieces; j += 1)
        {
            X86Instruction read;
            X86Instruction write;
            tier1_cert_emitted_piece(BUSTER_TIER1_SELECT_SCRATCH, X86_REGISTER_RSP, staged, source, parts[k].offset, parts[k].bytes, j, &read, &write);
            valid = tier1_cert_emitted_exactly(records[at + 2 * j], read) && tier1_cert_emitted_exactly(records[at + 2 * j + 1], write);
            valid = valid && tier1_cert_emitted_take_reference(build, at + 2 * j, 0) && tier1_cert_emitted_take_reference(build, at + 2 * j + 1, 0);
        }
        Tier1CertEmittedKernel kernel = {
            .referenced = UINT32_MAX,
            .definition = BUSTER_TIER1_CFG_NONE,
            .definition_location = BUSTER_CERT_NONE,
        };
        valid = valid && tier1_cert_emitted_add_use(&kernel, value, tier1_cert_emitted_gpr(source, 8)) && tier1_cert_emitted_originals(build, node, &kernel, records + at, 2 * pieces);
        valid = valid && tier1_cert_emitted_define(build, node, build->result_parts + k, tier1_cert_emitted_stack(build, staged, tier1_cert_emitted_part_width(vector, parts[k].bytes, false)));
        at += valid ? 2 * pieces : 0;
    }
    for (uint32_t k = 0; valid && k < part_count; k += 1)
    {
        const bool vector = (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        const bool direct = tier1_select_part_direct(parts[k]);
        const X86Instruction load = {
            .displacement = direct ? parts[k].offset : tier1_emit_staging_part(tier1_select_result_slot(parts, k), parts[k].bytes),
            .reg = (uint8_t)(parts[k].reg & 0x1f),
            .rm = direct ? source : X86_REGISTER_RSP,
            .mode = BUSTER_X86_MEMORY_BASE,
            .form = vector ? tier1_select_part_form(parts[k].bytes, true) : X86_FORM_MOV_R64_RM64,
        };
        valid = at < end && tier1_cert_emitted_exactly(records[at], load);
        if (valid && direct)
        {
            // A split vector's part (#1847): its load reads the result's bytes through the address and defines the part's
            // value in its register, which the ret reads (tier1_cert_emitted_return).
            Tier1CertEmittedKernel kernel = {
                .referenced = UINT32_MAX,
                .definition = BUSTER_TIER1_CFG_NONE,
                .definition_location = BUSTER_CERT_NONE,
            };
            valid = tier1_cert_emitted_take_reference(build, at, 0) && tier1_cert_emitted_add_use(&kernel, value, tier1_cert_emitted_gpr(source, 8)) &&
                    tier1_cert_emitted_originals(build, node, &kernel, records + at, 1);
            valid = valid && tier1_cert_emitted_define(build, node, build->result_parts + k, tier1_cert_emitted_vector(build, parts[k].reg & 0x1f, tier1_cert_emitted_part_width(true, parts[k].bytes, false)));
        }
        else if (valid)
        {
            valid = tier1_cert_emitted_append_move(build, at, BUSTER_IR_NONE, BUSTER_CERT_NONE);
        }
        at += 1;
    }
    int32_t hidden_offset = 0;
    valid = valid && (!hidden || tier1_emit_cfg_object(function, emitted, function->slot_count + function->parameter_count, &hidden_offset));
    if (valid && hidden)
    {
        const uint8_t holder = source == X86_REGISTER_RAX ? X86_REGISTER_R10 : X86_REGISTER_RAX;
        const X86Instruction load = { .displacement = hidden_offset, .reg = holder, .rm = build->base, .mode = BUSTER_X86_MEMORY_BASE, .form = X86_FORM_MOV_R64_RM64 };
        valid = at < end && tier1_cert_emitted_exactly(records[at], load) && tier1_cert_emitted_append_move(build, at, BUSTER_IR_NONE, tier1_cert_emitted_gpr(holder, 8));
        at += 1;
        const uint32_t pieces = tier1_cert_emitted_pieces(function->result_size);
        valid = valid && end - at >= 2 * pieces;
        for (uint32_t j = 0; valid && j < pieces; j += 1)
        {
            X86Instruction read;
            X86Instruction write;
            tier1_cert_emitted_piece(BUSTER_TIER1_SELECT_SCRATCH, holder, 0, source, 0, function->result_size, j, &read, &write);
            valid = tier1_cert_emitted_exactly(records[at + 2 * j], read) && tier1_cert_emitted_exactly(records[at + 2 * j + 1], write);
            valid = valid && tier1_cert_emitted_take_reference(build, at + 2 * j, 0) && tier1_cert_emitted_take_reference(build, at + 2 * j + 1, 0);
        }
        Tier1CertEmittedKernel kernel = {
            .referenced = UINT32_MAX,
            .definition = BUSTER_TIER1_CFG_NONE,
            .definition_location = BUSTER_CERT_NONE,
        };
        valid = valid && tier1_cert_emitted_add_use(&kernel, value, tier1_cert_emitted_gpr(source, 8)) &&
                tier1_cert_emitted_add_use(&kernel, build->parameter_parts + 2 * function->parameter_count, tier1_cert_emitted_gpr(holder, 8)) &&
                tier1_cert_emitted_originals(build, node, &kernel, records + at, 2 * pieces);
        at += valid ? 2 * pieces : 0;
        const X86Instruction move = tier1_emit_move(holder, X86_REGISTER_RAX, 8);
        valid = valid && (holder == X86_REGISTER_RAX || (at < end && tier1_cert_emitted_exactly(records[at], move) && tier1_cert_emitted_append_move(build, at, BUSTER_IR_NONE, tier1_cert_emitted_gpr(X86_REGISTER_RAX, 8))));
        at += holder == X86_REGISTER_RAX ? 0 : 1;
    }
    valid = valid && at == end;
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, at);
}

// Call node `node`'s steps (#789, #1284), from the first of its records on: those of the node before it all decoded.
static bool tier1_cert_emitted_steps_begin(Tier1CertEmittedBuild* build, uint32_t node)
{
    bool valid = true;
    if (build->step_node != node)
    {
        valid = build->step_node == BUSTER_IR_NONE || build->step_next == build->step_count;
        build->steps = build->call_steps[node];
        build->step_count = build->call_step_count[node];
        build->step_before = build->call_step_before[node];
        build->step_node = node;
        build->step_next = 0;
    }
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, UINT32_MAX);
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
    valid = valid && tier1_cert_emitted_add_use(&kernel, build->pseudo + 6, tier1_cert_emitted_entry_frame(build, 0, 8));
    // And an aggregate result (#789, #1284): its parts in their registers, or the hidden pointer in rax.
    const IrFunction* function = build->plan->function;
    Tier1SelectPart parts[BUSTER_TIER1_SELECT_RESULT_PARTS];
    const uint32_t part_count = function->result_size != 0 ? tier1_select_result_parts(function->result_abi, function->result_size, parts) : 0;
    for (uint32_t k = 0; valid && k < part_count; k += 1)
    {
        const bool vector = (parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
        const uint32_t width = tier1_cert_emitted_part_width(vector, parts[k].bytes, false);
        valid = tier1_cert_emitted_add_use(&kernel, build->result_parts + k, vector ? tier1_cert_emitted_vector(build, parts[k].reg & 0x1f, width) : tier1_cert_emitted_gpr(parts[k].reg, 8));
    }
    const bool hidden = function->result_size != 0 && function->result_abi.kind == BUSTER_ABI_RESULT_MEMORY;
    valid = valid && (!hidden || tier1_cert_emitted_add_use(&kernel, build->parameter_parts + 2 * function->parameter_count, tier1_cert_emitted_gpr(X86_REGISTER_RAX, 8)));
    return valid && tier1_cert_emitted_originals(build, node, &kernel, ret, 1);
}

// Save only a whole scalar result of a kernel whose concrete records were regenerated and compared. The caller
// publishes this array only after the independent value-flow certificate succeeds. An empty kernel, a machine row,
// a phi and an unsupported register class have no claim here. In particular, a call's interval includes its result
// conversion/copy, so that the call instruction's own PC can never stand for its future result.
static bool tier1_cert_emitted_debug_definition(Tier1CertEmittedBuild* build, uint32_t node, const Tier1CertEmittedKernel* kernel, uint32_t first, uint32_t end)
{
    const uint32_t type = build->plan->function->type[node];
    const uint32_t narrow = tier1_cert_emitted_narrow(type);
    const uint32_t bytes = narrow != 0 ? narrow : type == IR_I32 || type == IR_F32 ? 4 : type == IR_I64 || type == IR_F64 || type == IR_PTR ? 8 : 0;
    const bool floating = type == IR_F32 || type == IR_F64;
    bool valid = true;
    if (build->debug_definitions != NULL && bytes != 0 && kernel->definition != BUSTER_TIER1_CFG_NONE && first < end)
    {
        valid = kernel->definition_location < build->location_count && kernel->definition == build->emitted->node_values[node] &&
                end <= build->emitted->code.count && build->debug_definitions[node].first == UINT32_MAX;
        if (valid)
        {
            const Tier1CertLocation location = build->locations[kernel->definition_location];
            const bool supported = location.reg < 16 && location.width >= bytes &&
                                   location.class == (floating ? TIER1_CERT_VECTOR : TIER1_CERT_GPR);
            if (supported)
            {
                build->debug_definitions[node] = (Tier1DebugDefinition){
                    .first = first,
                    .end = end,
                    .reg = location.reg,
                    .class = floating ? BUSTER_TIER1_ALLOCATE_VECTOR : BUSTER_TIER1_ALLOCATE_GPR,
                    .bytes = (uint8_t)bytes,
                };
            }
        }
    }
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_ORIGIN, first);
}

// Node `node`'s run from record `r` before block tail `tail`: its records (their origin `origin`), a return's epilogue
// (which may have none) before its ret among them, as its kernel says; then its ORIGINALs. `next` is the record after
// the run.
static bool tier1_cert_emitted_run(Tier1CertEmittedBuild* build, uint32_t node, uint32_t origin, uint32_t r, uint32_t tail, uint16_t* condition, uint32_t* next)
{
    const Tier1EmitResult* emitted = build->emitted;
    const IrFunction* function = build->plan->function;
    const uint32_t epilogue_origin = tier1_origin(TIER1_ORIGIN_FRAME, 1);
    const uint32_t other_origin = tier1_origin(TIER1_ORIGIN_OTHER, node);
    const bool returns = function->op[node] == IR_RET;
    const bool calls = tier1_select_function_call(function, node);
    // A machine row's (#1284): its node's records, tier1_cert_emitted_row's.
    if (!build->plan->covered[node] && build->plan->patterns[node].rule == TIER1_RULE_ROW)
    {
        uint32_t end = r;
        while (end < tail && emitted->origins[end] == origin)
        {
            end += 1;
        }
        *next = end;
        return tier1_cert_emitted_row(build, node, r, end);
    }
    // An asm body's (#1839): its records, tier1_cert_emitted_asm's.
    if (!build->plan->covered[node] && build->plan->patterns[node].rule == TIER1_RULE_ASM)
    {
        uint32_t end = r;
        while (end < tail && emitted->origins[end] == origin)
        {
            end += 1;
        }
        *next = end;
        return tier1_cert_emitted_asm(build, node, r, end);
    }
    Tier1CertEmittedKernel kernel = { 0 };
    // A call's by-value aggregates' steps before it, all decoded (#789, #1284).
    bool valid = !calls || (tier1_cert_emitted_steps_begin(build, node) && (build->step_next == build->step_before || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r)));
    valid = valid && (tier1_cert_emitted_kernel(build, node, &kernel) || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, r));
    build->step_next = calls ? build->step_before : build->step_next;
    X86Instruction found[BUSTER_CERT_EMITTED_KERNEL];
    uint32_t found_count = 0;
    uint32_t at = r;
    bool epilogue = false;
    // The epilogue's records, and the records before it; a return's aggregate result's before them (#789, #1284).
    uint32_t epilogue_first = UINT32_MAX;
    uint32_t epilogue_end = UINT32_MAX;
    uint32_t result_first = UINT32_MAX;
    uint32_t result_end = UINT32_MAX;
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
        else if (here == other_origin && returns && !epilogue && result_first == UINT32_MAX && found_count + 1 == kernel.count)
        {
            result_first = at;
            while (at < tail && emitted->origins[at] == other_origin)
            {
                at += 1;
            }
            result_end = at;
        }
        else if (here == epilogue_origin && returns && !epilogue && found_count + 1 == kernel.count)
        {
            epilogue = true;
            epilogue_first = at;
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
        const uint32_t before = found_count - (found_count != 0);
        valid = valid && found_count != 0 && tier1_cert_emitted_originals(build, node, &kernel, found, before);
        // An aggregate result (#789, #1284): from its address, the ret's operand, in its register.
        const bool aggregate = function->result_size != 0;
        const uint32_t address = build->plan->patterns[node].values[0];
        const uint8_t source = kernel.use_count != 0 && build->locations[kernel.use_locations[0]].class == TIER1_CERT_GPR ? build->locations[kernel.use_locations[0]].reg : UINT8_MAX;
        valid = valid && (aggregate ? tier1_cert_emitted_result(build, node, address, source, result_first != UINT32_MAX ? result_first : at - 1, result_first != UINT32_MAX ? result_end : at - 1) : result_first == UINT32_MAX);
        valid = valid && (!epilogue || tier1_cert_emitted_epilogue(build, epilogue_first, epilogue_end));
        build->sp = epilogue ? build->sp : build->stack;
        valid = valid && tier1_cert_emitted_return(build, node, &found[found_count - 1], at - 1);
    }
    else
    {
        valid = valid && (!calls || tier1_cert_emitted_wholes(build, node));
        valid = valid && tier1_cert_emitted_originals(build, node, &kernel, found, found_count);
        // A call's aggregate result parts, in their registers after it.
        for (uint32_t k = 0; valid && k < kernel.result_count; k += 1)
        {
            valid = tier1_cert_emitted_define(build, node, kernel.results[k], kernel.result_locations[k]);
        }
        valid = valid && tier1_cert_emitted_debug_definition(build, node, &kernel, r, at);
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
    // A call's by-value aggregates' steps (#789, #1284) all decoded, its result's stores among them, before any record of
    // a later node's.
    bool valid = build->step_node == BUSTER_IR_NONE || node <= build->step_node || build->step_next == build->step_count || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_KERNEL, record);
    build->step_node = valid && build->step_node != BUSTER_IR_NONE && node > build->step_node ? BUSTER_IR_NONE : build->step_node;
    for (uint32_t n = *cursor; valid && n < node; n += 1)
    {
        // A machine row with no records of its node's (#1284): its moves into place alone, before it.
        const bool row = !plan->covered[n] && plan->patterns[n].rule == TIER1_RULE_ROW;
        // An asm body with no records of its node's (#1839): an empty template's, its saves and restores alone.
        const bool body = !plan->covered[n] && plan->patterns[n].rule == TIER1_RULE_ASM;
        bool silent = row || body || tier1_cert_emitted_silent(build->emitted, plan, n);
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
            valid = row ? tier1_cert_emitted_row(build, n, record, record) : body ? tier1_cert_emitted_asm(build, n, record, record) : !kept || (tier1_cert_emitted_kernel(build, n, &kernel) && kernel.count == 0);
            silent = row || body || !kept;
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

// The frame's ranges as tier1_certificate_emitted gathers them: the frame base register's displacements, and the offsets
// from rsp at entry that a realigned frame keeps apart (#1284).
typedef struct Tier1CertEmittedRange Tier1CertEmittedRange;
struct Tier1CertEmittedRange
{
    int64_t low;
    int64_t high;
    int64_t entry_low;
    int64_t entry_high;
};

// The bytes [start, end), the frame base register's displacements or (`entry`) offsets from rsp at entry, into the
// ranges: an entry's the frame base register's where the frame has their distance.
static void tier1_cert_emitted_range(const Tier1CertEmittedBuild* build, Tier1CertEmittedRange* range, bool entry, int64_t start, int64_t end)
{
    const bool apart = entry && build->realigned;
    const int64_t shift = entry && !build->realigned ? build->frame_base : 0;
    int64_t* low = apart ? &range->entry_low : &range->low;
    int64_t* high = apart ? &range->entry_high : &range->high;
    *low = start - shift < *low ? start - shift : *low;
    *high = end - shift > *high ? end - shift : *high;
}

// Record `r` of the entry block's body, its origin ENTRY's: a parameter's explicit representation conversion,
// which defines the parameter itself from only its caller's logical bytes, or a parameter's move into its register.
static bool tier1_cert_emitted_entry(Tier1CertEmittedBuild* build, uint32_t r)
{
    const Tier1EmitResult* emitted = build->emitted;
    const IrFunction* function = build->plan->function;
    const X86Instruction record = emitted->code.records[r];
    const uint32_t node = emitted->origin_values[r];
    const bool parameter = node < function->instruction_count && function->op[node] == IR_PARAM;
    const uint32_t type = parameter ? function->type[node] : IR_VOID;
    const bool signed_parameter = parameter && type == IR_I32 && build->plan->patterns[node].sign_result;
    const uint16_t extend = signed_parameter ? X86_FORM_MOVSXD_R64_RM32 : type == IR_I1 || type == IR_I8 ? X86_FORM_MOVZX_R32_RM8 : type == IR_I16 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_NONE;
    const Tier1SelectPlace place = parameter ? tier1_select_parameter_place(function, function->a[node]) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_STACK };
    const bool narrowed = extend != X86_FORM_NONE && record.form == extend && place.kind == BUSTER_TIER1_PLACE_GENERAL && record.reg == tier1_select_parameter_registers[place.index] && record.rm == record.reg &&
                          record.mode == BUSTER_X86_REGISTER_DIRECT;
    // A stack parameter's load from its incoming slot, by its type's form: a narrow one's zero extension into its canonical
    // form, an I32's 32-bit load, a 64-bit one's, a floating one's into an xmm register, or its bits into a general one.
    uint8_t base = 0;
    int32_t offset = 0;
    const bool incoming = place.kind == BUSTER_TIER1_PLACE_STACK && tier1_emit_cfg_incoming(emitted, (uint64_t)BUSTER_ABI_SLOT_BYTES * place.index, &base, &offset) && record.mode == BUSTER_X86_MEMORY_BASE && record.rm == base && record.displacement == offset;
    const uint32_t float_width = parameter ? tier1_select_float_width(type) : 0;
    const uint16_t general_form = signed_parameter ? extend : type == IR_I64 || type == IR_PTR || float_width == 8 ? X86_FORM_MOV_R64_RM64 : type == IR_I32 || float_width == 4 ? X86_FORM_MOV_R32_RM32 : extend;
    const uint16_t vector_form = float_width != 0 ? tier1_select_float_forms(float_width)->load : X86_FORM_NONE;
    bool valid = !incoming || record.form == general_form || record.form == vector_form;
    const uint32_t narrow = signed_parameter ? 4 : tier1_cert_emitted_narrow(type);
    const uint32_t value = parameter ? emitted->node_values[node] : BUSTER_TIER1_CFG_NONE;
    if (valid && ((incoming && extend != X86_FORM_NONE && record.reg < 16) || narrowed))
    {
        // Read only the caller's logical bytes and define the selected representation. Exact instruction form,
        // source and destination are required: omitting it or reading unspecified upper bytes defines no value.
        const uint32_t from = narrowed ? tier1_cert_emitted_low(build, record.reg, narrow) : tier1_cert_emitted_memory(build, record, narrow);
        Tier1CertSource* source = 0;
        Tier1CertInstruction* instruction = 0;
        valid = value != BUSTER_TIER1_CFG_NONE && from != BUSTER_CERT_NONE && tier1_cert_emitted_take_reference(build, r, 0) && tier1_cert_emitted_original(build, node, 6, 0, &source, &instruction);
        if (valid)
        {
            source->operands[0] = (Tier1CertOperand){
                .value = build->incoming + function->a[node],
                .allowed_registers = UINT32_MAX,
                .flags = narrowed ? 0 : BUSTER_CERT_MEMORY,
            };
            source->operands[1] = (Tier1CertOperand){
                .value = value,
                .allowed_registers = UINT32_MAX,
            };
            source->use_count = 1;
            source->def_count = 1;
            instruction->locations[0] = from;
            instruction->locations[1] = tier1_cert_emitted_gpr(record.reg, signed_parameter ? 8 : 4);
            instruction->operand_count = 2;
            build->stats->originals += 1;
        }
    }
    else if (valid)
    {
        valid = build->code_count < build->code_capacity && tier1_cert_emitted_move(build, r, BUSTER_IR_NONE, build->code + build->code_count);
        build->code_count += valid;
        build->stats->edits += valid;
    }
    return valid || tier1_cert_emitted_refuse(build, TIER1_CERT_EMITTED_DECODE, r);
}

static Tier1CertEmittedReason tier1_certificate_emitted_debug(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, Arena* output, Arena* scratch,
                                                            Tier1DebugDefinition* definitions, Tier1DebugPhi* phis, Tier1CertProgram* program, Tier1CertEmittedStats* stats)
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
        .debug_definitions = definitions,
        .stats = &tally,
        .base = (uint8_t)emitted->frame.base_register,
        .base_position = 2 * added,
    };
    for (uint32_t node = 0; definitions != NULL && node < count; node += 1)
    {
        definitions[node] = (Tier1DebugDefinition){ .first = UINT32_MAX, .end = UINT32_MAX };
    }
    for (uint32_t node = 0; phis != NULL && node < count; node += 1)
    {
        phis[node] = (Tier1DebugPhi){ .record = UINT32_MAX, .block = UINT32_MAX };
    }
    // A wide layout's vector registers (#1284) for a function whose vectors or rows need them.
    build.wide = tier1_cert_emitted_wide(plan, emitted);
    build.frame_unit = BUSTER_CERT_EMITTED_VECTOR_UNIT + (build.wide ? 32 * 64 : 16 * 16) + 8 * 8;
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
    build.incoming = values + BUSTER_CERT_EMITTED_PSEUDO;
    // The by-value aggregates' parts' values (#789, #1284): the parameters', two each, the hidden pointer's past them; the
    // function's result's, two; and each call's from its first, in its steps' order (tier1_cert_emitted_call_steps).
    build.parameter_parts = build.incoming + function->parameter_count;
    build.result_parts = build.parameter_parts + 2 * (function->parameter_count + 1);
    build.step_node = BUSTER_IR_NONE;
    uint32_t value_total = build.result_parts + BUSTER_TIER1_SELECT_RESULT_PARTS;
    build.call_first = valid ? arena_push_array(scratch, (uint64_t)count + 1, 4, 4) : 0;
    build.call_steps = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(Tier1CertEmittedStep*), _Alignof(Tier1CertEmittedStep*)) : 0;
    build.call_step_count = valid ? arena_push(scratch, (uint64_t)count + 1, 1) : 0;
    build.call_step_before = valid ? arena_push(scratch, (uint64_t)count + 1, 1) : 0;
    const bool step_arrays = build.call_first != 0 && build.call_steps != 0 && build.call_step_count != 0 && build.call_step_before != 0;
    valid = valid && (step_arrays || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
    // Each call's steps, computed once, kept where it has any (a call of no by-value aggregate has none).
    for (uint32_t node = 0; valid && node < count; node += 1)
    {
        build.call_first[node] = value_total;
        Tier1CertEmittedStep computed[BUSTER_CERT_EMITTED_STEPS];
        uint32_t step_count = 0;
        uint32_t before = 0;
        uint32_t pseudos = 0;
        const bool calls = !plan->covered[node] && tier1_select_function_call(function, node);
        valid = !calls || tier1_cert_emitted_call_steps(&build, node, value_total, computed, &step_count, &before, &pseudos) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_KERNEL, UINT32_MAX);
        Tier1CertEmittedStep* kept = valid && step_count != 0 ? arena_push_array(scratch, step_count, sizeof(Tier1CertEmittedStep), _Alignof(Tier1CertEmittedStep)) : 0;
        valid = valid && (step_count == 0 || kept != 0 || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
        if (valid && step_count != 0)
        {
            memcpy(kept, computed, (uint64_t)step_count * sizeof(Tier1CertEmittedStep));
        }
        build.call_steps[node] = kept;
        build.call_step_count[node] = (uint8_t)step_count;
        build.call_step_before[node] = (uint8_t)before;
        value_total += pseudos;
    }
    // The entry's steps: its by-value aggregate parameters' copies.
    const uint32_t entry_capacity = 4 * (function->parameter_count + 1);
    build.entry_steps = valid ? arena_push_array(scratch, entry_capacity, sizeof(Tier1CertEmittedStep), _Alignof(Tier1CertEmittedStep)) : 0;
    valid = valid && (build.entry_steps != 0 || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
    valid = valid && (tier1_cert_emitted_entry_steps(&build, build.parameter_parts, build.entry_steps, entry_capacity, &build.entry_count) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_KERNEL, UINT32_MAX));
    // The frame's storage: the frame base register's displacements the stream may name, and in a realigned frame (#1284)
    // the offsets from rsp at entry it may name past them (Tier1CertEmittedBuild.entry_low).
    const Tier1EmitPrepared* prepared = &emitted->prepared;
    Tier1CertEmittedRange range = {
        .low = INT64_MAX,
        .high = INT64_MIN,
        .entry_low = INT64_MAX,
        .entry_high = INT64_MIN,
    };
    // The storage rows, the spills', the saves' and the temporary slots', each its bytes (tier1_emit_cfg_row_bytes).
    const uint32_t rows = cfg->allocation.spill_count + (uint32_t)__builtin_popcount(prepared->call_save_mask) + (uint32_t)__builtin_popcount(prepared->mask_save_mask) + (uint32_t)__builtin_popcount(prepared->vector_save_mask) +
                          (uint32_t)__builtin_popcount(cfg->temp_slots & ((1u << BUSTER_TIER1_ALLOCATE_CLASSES) - 1));
    for (uint32_t row = 0; valid && row < rows && row < emitted->frame.count; row += 1)
    {
        tier1_cert_emitted_range(&build, &range, false, emitted->frame.offset[row], (int64_t)emitted->frame.offset[row] + tier1_emit_cfg_row_bytes(emitted, row));
    }
    // Overflow staging (#1450) is the last frame object, distinct from live spills and locals. Its stores and loads
    // define and consume the same part values as red-zone staging, and the frame boundary below covers its bytes too.
    if (valid && prepared->staging_slots != 0)
    {
        const Tier1EmitInto storage = tier1_emit_argument_staging(emitted, 0, 8);
        valid = storage.valid || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_FRAME, UINT32_MAX);
        if (valid)
        {
            tier1_cert_emitted_range(&build, &range, false, storage.displacement, (int64_t)storage.displacement + 8 * prepared->staging_slots);
        }
    }
    // Rows are bytes no callee writes: at or above rsp after the prologue, or in a function without calls in the red
    // zone, the ABI's 128 bytes below it. The certificate models no write below rsp but the code's own (a call's push, its
    // callee's frame, the staging slots' stores). In a realigned frame, rsp is the frame base register, and the rows are
    // below the pushes: past its allocation's bytes from rsp, its `and` may have moved rsp down to them.
    const int64_t lowest = (build.realigned ? 0 : build.stack - build.frame_base) - (prepared->call_count != 0 ? 0 : BUSTER_ABI_RED_ZONE_BYTES);
    valid = valid && ((range.low >= lowest && (!build.realigned || range.high <= (int64_t)emitted->frame.stack_adjust_bytes)) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_FRAME, UINT32_MAX));
    // And the calls' outgoing areas from rsp, the frame base register (tier1_cert_emitted_supported), and the stack
    // parameters' incoming slots, by the ABI 8 past the return address.
    for (uint32_t node = 0; valid && node < count; node += 1)
    {
        const uint32_t op = function->op[node];
        const uint32_t outgoing = (op == IR_CALL || op == IR_CALL_INDIRECT) && !plan->covered[node] ? tier1_select_call_stack_bytes(function, &function->calls[function->b[node]]) : 0;
        const Tier1SelectPlace place = tier1_select_entry_parameter(function, node) ? tier1_select_parameter_place(function, function->a[node]) : (Tier1SelectPlace){ .kind = BUSTER_TIER1_PLACE_GENERAL };
        if (outgoing != 0)
        {
            tier1_cert_emitted_range(&build, &range, false, 0, outgoing);
        }
        if (place.kind == BUSTER_TIER1_PLACE_STACK)
        {
            const int64_t slot = 8 + (int64_t)BUSTER_ABI_SLOT_BYTES * place.index;
            tier1_cert_emitted_range(&build, &range, true, slot, slot + BUSTER_ABI_SLOT_BYTES);
        }
    }
    // And the by-value aggregate parameters' parts in incoming slots (#1284: a SPLIT location's, a STACK one's high or low
    // eightbyte alone), each by the ABI 8 past the return address and its location's offset, which the layout's offset
    // its load uses must be, from the register it names, as a stack parameter's.
    for (uint32_t k = 0; valid && k < build.entry_count; k += 1)
    {
        const Tier1CertEmittedStep step = build.entry_steps[k];
        if (step.kind == TIER1_CERT_STEP_SLOT)
        {
            int64_t at = 0;
            bool entry = false;
            const bool placed = tier1_cert_emitted_place(&build, step.record.rm, step.record.displacement, &at, &entry);
            const bool incoming = placed && (entry ? at : build.frame_base == INT64_MIN ? INT64_MIN : build.frame_base + at) == step.to;
            valid = incoming || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_FRAME, UINT32_MAX);
            tier1_cert_emitted_range(&build, &range, true, step.to, (int64_t)step.to + BUSTER_ABI_SLOT_BYTES);
        }
    }
    // And the pushes' slots and the return address's, by their offsets from rsp at entry.
    for (uint32_t reg = 0; valid && reg < 16; reg += 1)
    {
        if (build.saves[reg] != INT64_MIN)
        {
            tier1_cert_emitted_range(&build, &range, true, build.saves[reg], build.saves[reg] + 8);
        }
    }
    if (valid)
    {
        tier1_cert_emitted_range(&build, &range, true, 0, 8);
    }
    // And the hidden pointer's object (#789), its own storage: the entry stores the pointer there and a return loads it.
    int32_t hidden_offset = 0;
    if (valid && function->result_size != 0 && function->result_abi.kind == BUSTER_ABI_RESULT_MEMORY && tier1_emit_cfg_object(function, emitted, function->slot_count + function->parameter_count, &hidden_offset))
    {
        tier1_cert_emitted_range(&build, &range, false, hidden_offset, (int64_t)hidden_offset + 8);
    }
    // And the red zone below rsp, where the code stages a by-value aggregate's parts and a vector constant's words (#789,
    // #141), wherever a record reaches below rsp.
    bool below = false;
    for (uint32_t r = 0; valid && !below && r < records; r += 1)
    {
        const X86Instruction record = code->records[r];
        below = record.mode == BUSTER_X86_MEMORY_BASE && record.rm == X86_REGISTER_RSP && record.displacement < 0;
    }
    int64_t red = 0;
    bool red_entry = false;
    if (valid && below && tier1_cert_emitted_place(&build, X86_REGISTER_RSP, -(int64_t)BUSTER_ABI_RED_ZONE_BYTES, &red, &red_entry))
    {
        tier1_cert_emitted_range(&build, &range, red_entry, red, red + BUSTER_ABI_RED_ZONE_BYTES);
    }
    range.low = range.low <= range.high ? range.low : 0;
    range.high = range.low <= range.high ? range.high : 0;
    range.entry_low = range.entry_low <= range.entry_high ? range.entry_low : 0;
    range.entry_high = range.entry_low <= range.entry_high ? range.entry_high : 0;
    valid = valid && range.low >= INT32_MIN && range.high <= INT32_MAX && range.entry_low >= INT32_MIN && range.entry_high <= INT32_MAX;
    build.low = valid ? (int32_t)range.low : 0;
    build.high = valid ? (int32_t)range.high : 0;
    build.entry_low = valid ? (int32_t)range.entry_low : 0;
    build.entry_high = valid ? (int32_t)range.entry_high : 0;
    const uint64_t frame_units = (uint64_t)((int64_t)build.high - build.low) + (uint64_t)((int64_t)build.entry_high - build.entry_low);
    valid = valid && (frame_units <= BUSTER_CERT_MAX_TOTAL - build.frame_unit || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
    // The facts: a block's for each interval that carries its value into it.
    uint64_t fact_capacity = 0;
    for (uint32_t i = 0; valid && i < cfg->intervals.count; i += 1)
    {
        for (uint32_t b = tier1_cfg_block_of(input, cfg->intervals.starts[i]); b < blocks && input->block_starts[b] < cfg->intervals.ends[i]; b += 1)
        {
            fact_capacity += 1;
        }
    }
    // Carried call-save rows are untrusted additional homes. Validate node/class/mask/frame indices before using
    // them, then let the checker prove their actual bytes along every incoming edge. Block zero has no saved rows.
    const uint32_t* const entry_rows = emitted->entry_rows;
    const uint32_t call_save_mask = prepared->call_save_mask;
    uint64_t row_facts = 0;
    for (uint32_t b = 0; valid && entry_rows != 0 && b < blocks; b += 1)
    {
        for (uint32_t reg = 0; valid && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
        {
            const uint32_t node = entry_rows[(uint64_t)b * BUSTER_TIER1_EMIT_GENERAL_REGISTERS + reg];
            if (node != BUSTER_IR_NONE)
            {
                const uint32_t v = node < count ? emitted->node_values[node] : BUSTER_TIER1_CFG_NONE;
                const uint64_t row = (uint64_t)emitted->allocation.spill_count + (uint32_t)__builtin_popcount(call_save_mask & ((1u << reg) - 1));
                valid = b != 0 && v < values && input->classes[v] == BUSTER_TIER1_ALLOCATE_GPR && (call_save_mask & (1u << reg)) != 0 && row < emitted->frame.count;
                valid = valid || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, UINT32_MAX);
                row_facts += 1;
            }
        }
    }
    fact_capacity += row_facts;
    // The arrays: the locations (the registers', then a frame range per record and per kernel's store at most), the
    // values, the facts, the source and code, the blocks, edges and renames; and the scratch maps.
    // Aggregate parts need their own locations and sources (#789, #1284), in addition to carried-row facts.
    valid = valid && (value_total >= build.parameter_parts || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
    const uint64_t parts = valid ? (uint64_t)value_total - build.parameter_parts : 0;
    // A block's entry fact of a spilled value takes a frame location of its own (#1871): fact_capacity, past the carried
    // rows' facts, bounds them.
    const uint64_t location_capacity = BUSTER_CERT_EMITTED_REGISTERS + 4 * (uint64_t)records + 8 * (uint64_t)blocks + 2 * (uint64_t)function->parameter_count + 64 + fact_capacity + 16 * parts;
    const uint64_t source_capacity = BUSTER_CERT_EMITTED_NODE_SOURCES * (uint64_t)count + 16 + 24 * parts;
    const uint64_t code_capacity = records + source_capacity;
    const uint64_t facts_capacity = fact_capacity + count + BUSTER_CERT_EMITTED_PSEUDO * ((uint64_t)blocks + 1) + build.entry_count + 1;
    valid = valid && ((location_capacity <= UINT32_MAX && source_capacity <= UINT32_MAX && code_capacity <= UINT32_MAX && facts_capacity <= UINT32_MAX) ||
                      tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_CAPACITY, UINT32_MAX));
    build.location_capacity = valid ? (uint32_t)location_capacity : 0;
    build.source_capacity = valid ? (uint32_t)source_capacity : 0;
    build.code_capacity = valid ? (uint32_t)code_capacity : 0;
    build.locations = valid ? arena_push_array(output, build.location_capacity, sizeof(Tier1CertLocation), _Alignof(Tier1CertLocation)) : 0;
    Tier1CertValue* cert_values = valid ? arena_push_array(output, (uint64_t)value_total + 1, sizeof(Tier1CertValue), _Alignof(Tier1CertValue)) : 0;
    Tier1CertFact* facts = valid ? arena_push_array(output, facts_capacity, sizeof(Tier1CertFact), _Alignof(Tier1CertFact)) : 0;
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
        // The registers' locations (BUSTER_CERT_EMITTED_GPR's layout): a vector register's of a width its layout hasn't
        // (past xmm15, or past 16 bytes, in a function that isn't wide) are its whole xmm register's, which nothing names.
        const uint32_t vector_bytes = build.wide ? 64 : 16;
        const uint32_t mask_unit = BUSTER_CERT_EMITTED_VECTOR_UNIT + (build.wide ? 32 : 16) * vector_bytes;
        for (uint32_t r = 0; r < BUSTER_CERT_EMITTED_REGISTERS; r += 1)
        {
            const bool gpr = r < BUSTER_CERT_EMITTED_VECTOR;
            const bool vector = r >= BUSTER_CERT_EMITTED_VECTOR && r < BUSTER_CERT_EMITTED_MASK;
            const uint32_t index = gpr ? r / 2 : vector ? (r - BUSTER_CERT_EMITTED_VECTOR) / 5 : (r - BUSTER_CERT_EMITTED_MASK) / 4;
            const uint32_t width = gpr ? (r % 2 == 0 ? 8 : 4) : vector ? 64u >> ((r - BUSTER_CERT_EMITTED_VECTOR) % 5) : 1u << ((r - BUSTER_CERT_EMITTED_MASK) % 4);
            const bool held = !vector || tier1_cert_emitted_vector(&build, index, width) == r;
            build.locations[r] = (Tier1CertLocation){
                .unit = gpr ? 8 * index : vector ? BUSTER_CERT_EMITTED_VECTOR_UNIT + vector_bytes * (held ? index : index & 15) : mask_unit + 8 * index,
                .width = (uint16_t)(held ? width : 16),
                .extent = (uint16_t)(vector ? vector_bytes : 8),
                .class = gpr ? TIER1_CERT_GPR : vector ? TIER1_CERT_VECTOR : TIER1_CERT_MASK,
                .reg = (uint8_t)(held ? index : index & 15),
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
        // The callee-saved registers' values at entry and the return address; then each parameter's as its caller passed
        // it, a narrow one's at its ABI width.
        for (uint32_t k = 0; k < BUSTER_CERT_EMITTED_PSEUDO; k += 1)
        {
            cert_values[values + k] = (Tier1CertValue){
                .width = 8,
                .class = TIER1_CERT_GPR,
            };
        }
        for (uint32_t p = 0; p < function->parameter_count; p += 1)
        {
            const uint32_t narrow = tier1_cert_emitted_narrow(function->parameter_types[p]);
            cert_values[build.incoming + p] = (Tier1CertValue){
                .width = (uint16_t)(narrow != 0 ? narrow : 8),
                .class = TIER1_CERT_GPR,
            };
        }
        // The by-value aggregates' parts (#789, #1284): 8 bytes of a general register's by default, and each step's width
        // and class, the parameters', the result's and each call's.
        for (uint32_t v = build.parameter_parts; v < value_total; v += 1)
        {
            cert_values[v] = (Tier1CertValue){
                .width = 8,
                .class = TIER1_CERT_GPR,
            };
        }
        tier1_cert_emitted_step_values(build.entry_steps, build.entry_count, cert_values);
        Tier1SelectPart result_parts[BUSTER_TIER1_SELECT_RESULT_PARTS];
        const uint32_t result_part_count = function->result_size != 0 ? tier1_select_result_parts(function->result_abi, function->result_size, result_parts) : 0;
        for (uint32_t k = 0; k < result_part_count; k += 1)
        {
            const bool vector = (result_parts[k].reg & 0xe0) == BUSTER_ABI_REGISTER_VECTOR;
            cert_values[build.result_parts + k] = (Tier1CertValue){
                .width = tier1_cert_emitted_part_width(vector, result_parts[k].bytes, false),
                .class = vector ? TIER1_CERT_VECTOR : TIER1_CERT_GPR,
            };
        }
        for (uint32_t node = 0; node < count; node += 1)
        {
            tier1_cert_emitted_step_values(build.call_steps[node], build.call_step_count[node], cert_values);
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
        valid = kind > TIER1_ORIGIN_NONE && kind <= TIER1_ORIGIN_OTHER && (kind != TIER1_ORIGIN_EDGE || payload < edges) && ((kind != TIER1_ORIGIN_NODE && kind != TIER1_ORIGIN_OTHER) || payload < count) &&
                (kind != TIER1_ORIGIN_FRAME || payload <= 1);
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
        // An asm body's own (#1839), which tier1_cert_emitted_asm matches with the body's branches.
        const uint32_t node = emitted->origins[r] & BUSTER_TIER1_ORIGIN_PAYLOAD;
        const bool body = kind == TIER1_ORIGIN_NODE && node < plan->function->instruction_count && !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ASM;
        valid = valid && (kind == TIER1_ORIGIN_CONTROL || body || (kind == TIER1_ORIGIN_FRAME && t < records && emitted->origins[t] == emitted->origins[r]));
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
            // A stack parameter is its incoming slot's bytes: by the ABI 8 past the return address's, from rsp at entry. The
            // layout's offset, which its load uses, must be that from the register it names, where the decoded prologue
            // left it: the frame base register, or rbp where it is the frame pointer (a realigned frame's, #1284).
            const int64_t slot = 8 + (int64_t)BUSTER_ABI_SLOT_BYTES * place.index;
            int64_t at = 0;
            bool entry = false;
            const bool placed = stacked && tier1_emit_cfg_incoming(emitted, (uint64_t)BUSTER_ABI_SLOT_BYTES * place.index, &base, &offset) && tier1_cert_emitted_place(&build, base, offset, &at, &entry);
            const bool incoming = placed && (entry ? at : build.frame_base == INT64_MIN ? INT64_MIN : build.frame_base + at) == slot;
            valid = !stacked || incoming || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_FRAME, UINT32_MAX);
            // A narrow one is only its caller's bytes, its own value at entry (tier1_cert_emitted_entry's extension reads
            // them into its canonical storage).
            const bool signed_parameter = function->type[node] == IR_I32 && plan->patterns[node].sign_result;
            const uint32_t narrow = signed_parameter ? 4 : tier1_cert_emitted_narrow(function->type[node]);
            if (signed_parameter)
            {
                cert_values[build.incoming + function->a[node]].width = 4;
            }
            const uint32_t width = narrow != 0 ? narrow : tier1_cert_emitted_width(emitted, v);
            const uint32_t location = stacked ? (incoming ? tier1_cert_emitted_entry_frame(&build, slot, width) : BUSTER_CERT_NONE)
                                    : narrow != 0 && general ? tier1_cert_emitted_low(&build, reg, narrow)
                                    : tier1_cert_emitted_register(&build, v, 32 * (general ? BUSTER_TIER1_ALLOCATE_GPR : BUSTER_TIER1_ALLOCATE_VECTOR) + reg);
            facts[entry_count] = (Tier1CertFact){
                .value = narrow != 0 ? build.incoming + function->a[node] : v,
                .location = location,
            };
            valid = valid && (facts[entry_count].location != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_DECODE, UINT32_MAX));
            entry_registers |= stacked ? 0 : general ? BUSTER_X86_RESOURCE_GPR(reg) : BUSTER_X86_RESOURCE_VECTOR(reg);
            entry_count += 1;
        }
    }
    // The by-value aggregate parameters' parts (#789, #1284) where their caller passed them, which the prologue writes
    // none of: in their registers, or an incoming slot (whose load into the scratch register its store reads), and the
    // hidden pointer in rdi.
    for (uint32_t k = 0; valid && k < build.entry_count; k += 1)
    {
        const Tier1CertEmittedStep step = build.entry_steps[k];
        const bool registered = step.kind == TIER1_CERT_STEP_POINTER || (step.kind == TIER1_CERT_STEP_PARAMETER && !step.slot);
        const uint32_t location = step.kind == TIER1_CERT_STEP_SLOT ? tier1_cert_emitted_memory(&build, step.record, 8)
                                  : registered && step.vector ? tier1_cert_emitted_vector(&build, step.reg, step.width)
                                  : registered ? tier1_cert_emitted_gpr(step.reg, 8)
                                               : BUSTER_CERT_NONE;
        if (step.kind == TIER1_CERT_STEP_SLOT || registered)
        {
            facts[entry_count] = (Tier1CertFact){
                .value = step.pseudo,
                .location = location,
            };
            valid = location != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_DECODE, UINT32_MAX);
            entry_registers |= !registered ? 0 : step.vector ? BUSTER_X86_RESOURCE_VECTOR(step.reg) : BUSTER_X86_RESOURCE_GPR(step.reg);
            entry_count += 1;
        }
    }
    // The callee-saved registers' values at entry in their registers, and the return address at rsp.
    for (uint32_t k = 0; valid && k < BUSTER_CERT_EMITTED_PSEUDO; k += 1)
    {
        facts[entry_count] = (Tier1CertFact){
            .value = values + k,
            .location = k < 6 ? tier1_cert_emitted_gpr(tier1_cert_emitted_callee[k], 8) : tier1_cert_emitted_entry_frame(&build, 0, 8),
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
                        .location = k < 6 && at == INT64_MIN ? tier1_cert_emitted_gpr(reg, 8) : tier1_cert_emitted_entry_frame(&build, at, 8),
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
                        valid = tier1_cfg_entry_location_oracle(cfg, v, entry, &where, &kind);
                        if (valid && kind == BUSTER_TIER1_ALLOCATE_REGISTER)
                        {
                            location = tier1_cert_emitted_register(&build, v, where);
                            valid = location != BUSTER_CERT_NONE;
                            if (valid && phis != NULL && is_phi[v] != 0 && input->definitions[v] == entry && b >= added)
                            {
                                const uint32_t node = emitted->value_nodes[v];
                                const uint32_t type = function->type[node];
                                const uint32_t narrow = tier1_cert_emitted_narrow(type);
                                const uint32_t bytes = narrow != 0 ? narrow : type == IR_I32 || type == IR_F32 ? 4 : type == IR_I64 || type == IR_PTR || type == IR_F64 ? 8 : 0;
                                const uint32_t class = where / 32;
                                const uint32_t reg = where % 32;
                                if (bytes != 0 && reg < 16 && class <= BUSTER_TIER1_ALLOCATE_VECTOR)
                                {
                                    phis[node] = (Tier1DebugPhi){ .record = UINT32_MAX, .block = b - added, .reg = (uint8_t)reg, .class = (uint8_t)class, .bytes = (uint8_t)bytes };
                                }
                            }
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
        for (uint32_t b = 1; valid && entry_rows != 0 && b < blocks; b += 1)
        {
            for (uint32_t reg = 0; valid && numbers[b] != BUSTER_CERT_NONE && reg < BUSTER_TIER1_EMIT_GENERAL_REGISTERS; reg += 1)
            {
                const uint32_t node = entry_rows[(uint64_t)b * BUSTER_TIER1_EMIT_GENERAL_REGISTERS + reg];
                if (node != BUSTER_IR_NONE)
                {
                    if (pass == 0)
                    {
                        fact_offsets[b + 1] += 1;
                    }
                    else
                    {
                        const uint32_t v = emitted->node_values[node];
                        const X86Instruction save = tier1_emit_call_save(emitted, reg, false);
                        const uint32_t location = tier1_cert_emitted_frame(&build, save.displacement, tier1_cert_emitted_width(emitted, v));
                        facts[fill[b]++] = (Tier1CertFact){
                            .value = v,
                            .location = location,
                        };
                        valid = location != BUSTER_CERT_NONE || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_DECODE, UINT32_MAX);
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
        // The block's entry certificate describes state after those leading edge moves. The checker below proves
        // every incoming edge's actual copies and simultaneous phi renames before accepting these candidates.
        for (uint32_t k = input->phi_offsets[b]; live && valid && phis != NULL && k < input->phi_offsets[b + 1]; k += 1)
        {
            const uint32_t node = emitted->value_nodes[input->phi_values[k]];
            if (phis[node].block != UINT32_MAX)
            {
                phis[node].record = lead;
            }
        }
        uint32_t tail = lead;
        for (uint32_t r = lead; r < end; r += 1)
        {
            const Tier1Origin kind = tier1_cert_emitted_kind(emitted->origins[r]);
            tail = kind != TIER1_ORIGIN_EDGE && kind != TIER1_ORIGIN_CONTROL ? r + 1 : tail;
        }
        bool body = false;
        uint16_t condition = X86_FORM_NONE;
        // The node whose run last wrote the flags, or UINT32_MAX where another record did since (a move's xor); and
        // whether the block's conditional branch ran its own comparison.
        uint32_t flags_node = UINT32_MAX;
        bool compared = false;
        for (uint32_t r = lead; live && valid && r < tail; r += 1)
        {
            const uint32_t origin = emitted->origins[r];
            const Tier1Origin kind = tier1_cert_emitted_kind(origin);
            const uint32_t payload = origin & BUSTER_TIER1_ORIGIN_PAYLOAD;
            const uint32_t from = r;
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
                if (valid && build.entry_next < build.entry_count && emitted->origin_values[r] == BUSTER_IR_NONE)
                {
                    // A by-value aggregate parameter's copy (#789, #1284), the entry's first records: its steps in order.
                    uint32_t run = r;
                    while (run < tail && emitted->origins[run] == origin)
                    {
                        run += 1;
                    }
                    uint32_t at = r;
                    valid = tier1_cert_emitted_step(&build, count, &build.entry_steps[build.entry_next], &at, run);
                    build.entry_next += 1;
                    r = valid ? at - 1 : r;
                }
                else
                {
                    valid = valid && tier1_cert_emitted_entry(&build, r);
                }
                body = true;
            }
            else if (kind == TIER1_ORIGIN_OTHER && payload < count && function->op[payload] == IR_RET)
            {
                // A return's aggregate result (#789, #1284): its run's, from here (tier1_cert_emitted_run).
                valid = (payload >= cursor && payload < node_end) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, r);
                valid = valid && tier1_cert_emitted_flush(&build, &cursor, payload, r, degree, &condition);
                uint32_t next = r + 1;
                valid = valid && tier1_cert_emitted_run(&build, payload, tier1_origin(TIER1_ORIGIN_NODE, payload), r, tail, &condition, &next);
                cursor = payload + 1;
                r = next - 1;
                body = true;
            }
            else if (kind == TIER1_ORIGIN_OTHER)
            {
                // A call's by-value aggregates' records (#789, #1284): its steps, before the call and, after its run, its
                // result's stores.
                valid = (payload + 1 >= cursor && payload < node_end && tier1_select_function_call(function, payload)) || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_ORIGIN, r);
                valid = valid && tier1_cert_emitted_flush(&build, &cursor, payload, r, degree, &condition) && tier1_cert_emitted_steps_begin(&build, payload);
                uint32_t run = r;
                while (run < tail && emitted->origins[run] == origin)
                {
                    run += 1;
                }
                // Before the call's run, only the steps before it.
                const uint32_t last = cursor > payload ? build.step_count : build.step_before;
                uint32_t at = r;
                while (valid && at < run)
                {
                    valid = (build.step_next < last || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_KERNEL, at)) && tier1_cert_emitted_step(&build, payload, &build.steps[build.step_next], &at, run);
                    build.step_next += 1;
                }
                r = run - 1;
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
            // The records just decoded, [from, r]: a node's run that writes the flags leaves them its own, and any other
            // record that does leaves them no node's.
            bool writes = false;
            for (uint32_t k = from; valid && k <= r && k < tail; k += 1)
            {
                writes = writes || tier1_cert_emitted_writes_flags(&code->records[k]);
            }
            flags_node = writes ? (kind == TIER1_ORIGIN_NODE ? payload : UINT32_MAX) : flags_node;
            compared = compared || (kind == TIER1_ORIGIN_NODE && function->op[payload] == IR_CBR);
        }
        // The flags the tail's conditional jumps read: the branch's own comparison's where it ran one, else (a branch
        // with no records but its jumps) the last node's that wrote them; never a move's.
        const bool flags = flags_node != UINT32_MAX && (!compared || function->op[flags_node] == IR_CBR);
        // The block's nodes past its last record; and the entry's copies, all of them (#789, #1284).
        valid = valid && (!live || tier1_cert_emitted_flush(&build, &cursor, node_end, tail, degree, &condition));
        valid = valid && (!live || b != 0 || build.entry_next == build.entry_count || tier1_cert_emitted_refuse(&build, TIER1_CERT_EMITTED_KERNEL, tail));
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
            valid = tier1_cert_emitted_walk(&build, b, e, e == input->successor_offsets[b], degree == 2 ? condition : X86_FORM_NONE, flags, tail, &taken);
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
        const uint32_t units = build.frame_unit + (uint32_t)((int64_t)build.high - build.low) + (uint32_t)((int64_t)build.entry_high - build.entry_low);
        *program = (Tier1CertProgram){
            .locations = build.locations,
            .values = cert_values,
            .blocks = cert_blocks,
            .entry = facts,
            .unit_count = units,
            .location_count = build.location_count,
            .value_count = value_total,
            .block_count = reachable,
            .entry_count = entry_count,
            .flags_location = BUSTER_CERT_NONE,
            .features = BUSTER_CERT_AVX512F | BUSTER_CERT_AVX512DQ | BUSTER_CERT_AVX512BW | BUSTER_CERT_AVX2 | BUSTER_CERT_AVX512VL,
        };
        tally.locations = build.location_count;
        tally.units = units;
        tally.bytes = (uint64_t)build.location_count * sizeof(Tier1CertLocation) + (uint64_t)value_total * sizeof(Tier1CertValue) + tally.facts * sizeof(Tier1CertFact) + (uint64_t)build.source_count * sizeof(Tier1CertSource) +
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

static Tier1CertEmittedReason tier1_certificate_emitted(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, Arena* output, Arena* scratch,
                                                      Tier1CertProgram* program, Tier1CertEmittedStats* stats)
{
    return tier1_certificate_emitted_debug(plan, emitted, output, scratch, NULL, NULL, program, stats);
}

// Publishes the certificate of the emitted code and checks it: OK, an unsupported shape's reason (not certified, not
// refused), or a refusal: the decoding's, or TIER1_CERT_EMITTED_CHECK with the checker's error in `report`. Both arenas
// are restored.
// `definitions`, when present, points to instruction_count entries owned outside these temporary arena tails. The
// entries and the optional node-indexed phi carriers remain candidates until the returned reason is OK; callers
// must not publish any of them on another reason.
static Tier1CertEmittedReason tier1_certificate_emitted_check_debug(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, Arena* output, Arena* scratch,
                                                                  Tier1DebugDefinition* definitions, Tier1DebugPhi* phis, Tier1CertReport* report, Tier1CertEmittedStats* stats)
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
    Tier1CertEmittedReason reason = tier1_certificate_emitted_debug(plan, emitted, output, scratch, definitions, phis, &program, &tally);
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

static Tier1CertEmittedReason tier1_certificate_emitted_check(const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, Arena* output, Arena* scratch,
                                                            Tier1CertReport* report, Tier1CertEmittedStats* stats)
{
    return tier1_certificate_emitted_check_debug(plan, emitted, output, scratch, NULL, NULL, report, stats);
}
