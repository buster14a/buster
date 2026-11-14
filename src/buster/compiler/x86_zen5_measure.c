// Copyright (c) 2026 David Gonzalez Martin
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// measure-x86-zen5 (#181): the 9700X microbenchmarks behind x86_zen5_rows.c. For each row's case (a form and an
// operand kind) it builds loops of BUSTER_X86_COST_BLOCK units from encoder records, runs them under one perf group
// (core cycles, retired instructions, retired macro-ops, macro-ops dispatched to the FP unit and to the integer unit)
// and reduces the medians to the row's columns. Include after x86_semantics.c and x86_target_forms.c: the fixtures
// come from the form table and the effects tables, not from per-mnemonic code, except the families listed below.
//
// Layout: experiments and shapes (X86CostShape, x86_zen5_shape) -> registers and records (x86_zen5_record,
// x86_zen5_bridge) -> units (x86_zen5_unit and its special families) -> programs (x86_zen5_program) -> counters
// (x86_zen5_counters, x86_zen5_sample) -> reduction (x86_zen5_reduce) -> the command (run_x86_zen5_measure).
//
// Experiments: twelve and six independent instances (throughput, ops, dispatch), a chain through each source role
// whose register the previous instance's result feeds (with a bridge record where the files differ: movq, kmovq,
// cmov), the destination's own chain, LEA's index chain, a memory kind's address chain (result -> and 0 -> add to the
// base), the status flags' chain (flags -> cmov -> the source), and one mix per blocker of known pipes. Special
// families: implicit registers (mul, div, cqo, accumulator immediates, cmpxchg), the stack (push, pop), control flow
// (jmp, jcc, call, ret), the x87 stack, the gather, and traps (ud2, never run). docs/x86-zen5.md states every
// assumption a derived column rests on. Only measure-x86-zen5 requires a 9700X; test-x86-zen5 builds, validates and
// runs every fixture its host's features allow, and never labels another CPU's results as Zen 5.

#include <cpuid.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#define BUSTER_X86_COST_BLOCK 240
#define BUSTER_X86_COST_CODE_SIZE 65536
#define BUSTER_X86_COST_RECORDS 2048
#define BUSTER_X86_COST_MAX_PASSES 31
// The most loop iterations a call runs (`--iterations`).
#define BUSTER_X86_COST_MAX_ITERATIONS 10000000
#define BUSTER_X86_COST_COUNTERS 5
// The data a program addresses through RSI: the targets' memory operands (in its first 2 KiB), the blockers' (2176 B
// on modulo 4 KiB, so that no blocker's access aliases a target's in the low 12 address bits, which the store queue
// compares first), and the constants.
#define BUSTER_X86_COST_DATA_SIZE 65536
#define BUSTER_X86_COST_DATA_TARGETS 2048
#define BUSTER_X86_COST_DATA_BLOCKER 18560
#define BUSTER_X86_COST_DATA_CONSTANTS 32768
#define BUSTER_X86_COST_DATA_SLOTS 64
#define BUSTER_X86_COST_POOL 12
_Static_assert(BUSTER_X86_COST_BLOCK % 12 == 0 && BUSTER_X86_COST_BLOCK % 10 == 0 && BUSTER_X86_COST_BLOCK % 8 == 0, "every ring closes across the back edge");
_Static_assert(4 * BUSTER_X86_COST_BLOCK + 200 <= BUSTER_X86_COST_RECORDS, "records hold the largest body and its wrapper");

typedef enum X86CostExperiment
{
    X86_COST_INDEPENDENT,
    X86_COST_INDEPENDENT_SPARE,
    X86_COST_CHAIN_DESTINATION,
    X86_COST_CHAIN_REG,
    X86_COST_CHAIN_RM,
    X86_COST_CHAIN_VVVV,
    X86_COST_CHAIN_INDEX,
    X86_COST_CHAIN_ADDRESS,
    X86_COST_CHAIN_FLAGS,
    X86_COST_MIX_FIRST,
    X86_COST_EXPERIMENT_COUNT = X86_COST_MIX_FIRST + BUSTER_X86_PIPE_BLOCKERS,
} X86CostExperiment;

static const char* const x86_zen5_experiment_names[X86_COST_EXPERIMENT_COUNT] = {
    "independent",
    "independent-spare",
    "chain-destination",
    "chain-reg",
    "chain-rm",
    "chain-vvvv",
    "chain-index",
    "chain-address",
    "chain-flags",
    "mix-alu",
    "mix-multiply",
    "mix-shift",
    "mix-load",
    "mix-store",
    "mix-fadd",
    "mix-fmul",
    "mix-vector-alu",
    "mix-shuffle",
    "mix-transfer",
};

typedef enum X86CostFile
{
    X86_COST_FILE_NONE,
    X86_COST_FILE_GPR,
    X86_COST_FILE_VECTOR,
    X86_COST_FILE_OPMASK,
    X86_COST_FILE_X87,
    X86_COST_FILE_FLAGS,
} X86CostFile;

typedef enum X86CostSpecial
{
    X86_COST_GENERIC,
    // RAX, RDX: mul and imul of one operand, div, idiv, cqo, cdq, the accumulator's immediates, cmpxchg.
    X86_COST_IMPLICIT,
    X86_COST_STACK,
    X86_COST_CONTROL,
    X86_COST_X87,
    X86_COST_GATHER,
    // ud2 and int3: never run.
    X86_COST_TRAP,
} X86CostSpecial;

// The roles in X86Instruction's fields, BUSTER_X86_ROLE_REG, _RM and _VVVV, and the is4 register in the immediate.
#define BUSTER_X86_COST_ROLES 3
#define BUSTER_X86_COST_NO_ROLE 0xff

// The constant registers every fixture reads and none writes: R14 holds 3, RSI the data, R12 counts iterations.
#define BUSTER_X86_COST_CONSTANT X86_REGISTER_R14
#define BUSTER_X86_COST_DATA X86_REGISTER_RSI
#define BUSTER_X86_COST_COUNTER X86_REGISTER_R12
// Vector registers 0 to 11 are written, 12 by the mixes' blockers, 13 to 15 the constants (is4, vvvv and rm sources).
// Opmasks 1 to 5 are written, 6 and 7 the constants.
#define BUSTER_X86_COST_VECTOR_BLOCKER 12
#define BUSTER_X86_COST_VECTOR_IS4 13
#define BUSTER_X86_COST_VECTOR_VVVV 14
#define BUSTER_X86_COST_VECTOR_RM 15
#define BUSTER_X86_COST_OPMASKS 5
#define BUSTER_X86_COST_OPMASK_VVVV 6
#define BUSTER_X86_COST_OPMASK_RM 7
// The opmask a masked case's target runs under (merging, or zeroing for a masked load): alternate elements (0x5555...),
// as its initial pattern.
#define BUSTER_X86_COST_OPMASK_MERGE 7

static const uint8_t x86_zen5_gpr_order[BUSTER_X86_COST_POOL] = {
    X86_REGISTER_RAX,
    X86_REGISTER_RDX,
    X86_REGISTER_R8,
    X86_REGISTER_R9,
    X86_REGISTER_R10,
    X86_REGISTER_R11,
    X86_REGISTER_RBX,
    X86_REGISTER_RBP,
    X86_REGISTER_RCX,
    X86_REGISTER_RDI,
    X86_REGISTER_R13,
    X86_REGISTER_R15,
};

// How a case's memory slots and registers start: the element of its floating-point operation, and whether its
// constant sources are additive identities (0.0) or multiplicative ones (1.0). Integers are 3.
typedef enum X86CostFill
{
    X86_COST_FILL_INTEGER,
    X86_COST_FILL_FLOAT,
    X86_COST_FILL_DOUBLE,
} X86CostFill;

typedef struct X86CostShape X86CostShape;
struct X86CostShape
{
    const X86FormDescription* description;
    const X86Effects* effects;
    uint32_t form;
    uint32_t operands;
    uint8_t files[BUSTER_X86_COST_ROLES];
    uint8_t access[BUSTER_X86_COST_ROLES];
    // The register role the form writes (its result), or BUSTER_X86_COST_NO_ROLE; a second written one for xchg and
    // xadd.
    uint8_t destination;
    uint8_t second;
    // Whether ModRM.rm is the memory operand in this case, and how the form accesses it.
    bool memory;
    bool loads;
    bool stores;
    bool lea;
    bool is4;
    // An opmask (BUSTER_X86_COST_OPMASK_MERGE) on the target, and its decorations: merging on registers and stores,
    // zeroing on a load into a vector register (the model's masked load row).
    bool masked;
    uint8_t decorations;
    uint8_t special;
    uint8_t fill;
    // Additive identity as the constant sources.
    bool additive;
    uint8_t flags_written;
    uint8_t flags_read;
    // General registers the form names implicitly (RAX, RDX, RCX), which no pool register may be.
    uint16_t fixed;
    uint8_t pool[BUSTER_X86_COST_POOL];
    uint8_t pool_count;
    int32_t immediate;
    // X86CostExperiment bits the case runs.
    uint32_t experiments;
};

// The blockers, one per BUSTER_X86_PIPE_* bit, in that order: write-only instructions of their unit's pipes, so that
// six of them saturate it. Their destinations are the pool's slots from 6 on.
typedef struct X86CostBlocker X86CostBlocker;
struct X86CostBlocker
{
    uint16_t form;
    uint8_t file;
    bool writes_flags;
};

static const X86CostBlocker x86_zen5_blockers[BUSTER_X86_PIPE_BLOCKERS] = {
    {
        .form = X86_FORM_MOV_R32_IMM32,
        .file = X86_COST_FILE_GPR,
    },
    {
        .form = X86_FORM_IMUL_R64_RM64_IMM8,
        .file = X86_COST_FILE_GPR,
        .writes_flags = true,
    },
    {
        .form = X86_FORM_RORX_R64_RM64_IMM8,
        .file = X86_COST_FILE_GPR,
    },
    {
        .form = X86_FORM_MOV_R64_RM64,
        .file = X86_COST_FILE_GPR,
    },
    {
        .form = X86_FORM_MOV_RM64_R64,
        .file = X86_COST_FILE_NONE,
    },
    {
        .form = X86_FORM_VADDPS_X_X_XM128,
        .file = X86_COST_FILE_VECTOR,
    },
    {
        .form = X86_FORM_VMULPS_X_X_XM128,
        .file = X86_COST_FILE_VECTOR,
    },
    {
        .form = X86_FORM_VPADDD_X_X_XM128,
        .file = X86_COST_FILE_VECTOR,
    },
    {
        .form = X86_FORM_VPSHUFB_X_X_XM128,
        .file = X86_COST_FILE_VECTOR,
    },
    {
        .form = X86_FORM_VMOVD_RM32_X,
        .file = X86_COST_FILE_GPR,
    },
};

typedef struct X86CostProgram X86CostProgram;
struct X86CostProgram
{
    uint8_t* code;
    uint32_t length;
    // The records of the loop's body, and those it executes each iteration outside them (a call's ret stub).
    uint32_t body_count;
    uint32_t extra_count;
    // Records per iteration that aren't the measured units' targets but are subtracted as known: the preparation of
    // an implicit register, a gather's mask, an x87 unit's balancing push or pop.
    uint32_t helper_count;
    uint64_t hash;
    // Whether the host runs every record (x86_zen5_program builds and checks a program on any host).
    bool runnable;
};

// What a body's record is: a measured unit's target, a helper the reduction accounts for, or a mix's blocker.
#define BUSTER_X86_COST_KIND_TARGET 0
#define BUSTER_X86_COST_KIND_HELPER 1
#define BUSTER_X86_COST_KIND_BLOCKER 2

typedef struct X86CostBuilder X86CostBuilder;
struct X86CostBuilder
{
    X86Instruction records[BUSTER_X86_COST_RECORDS];
    uint8_t kinds[BUSTER_X86_COST_RECORDS];
    // A record whose relative displacement (a branch's, a call's or a RIP-relative LEA's) reaches another record.
    uint16_t fixup_records[BUSTER_X86_COST_BLOCK + 8];
    uint16_t fixup_targets[BUSTER_X86_COST_BLOCK + 8];
    uint32_t count;
    uint32_t fixups;
    uint32_t helpers;
    bool valid;
};

static void x86_zen5_push_kind(X86CostBuilder* builder, X86Instruction record, uint8_t kind)
{
    if (builder->count < BUSTER_X86_COST_RECORDS)
    {
        builder->records[builder->count] = record;
        builder->kinds[builder->count] = kind;
        builder->count += 1;
    }
    else
    {
        builder->valid = false;
    }
}

// A measured unit's target record (or a wrapper's, outside the body).
static void x86_zen5_push(X86CostBuilder* builder, X86Instruction record)
{
    x86_zen5_push_kind(builder, record, BUSTER_X86_COST_KIND_TARGET);
}

// A helper record: one the reduction subtracts as a known count (not a measured unit's target).
static void x86_zen5_helper(X86CostBuilder* builder, X86Instruction record)
{
    x86_zen5_push_kind(builder, record, BUSTER_X86_COST_KIND_HELPER);
    builder->helpers += 1;
}

// The record at `record` reaches the record `target` (an index, possibly past the current end).
static void x86_zen5_fixup(X86CostBuilder* builder, uint32_t record, uint32_t target)
{
    if (builder->fixups < BUSTER_ARRAY_LENGTH(builder->fixup_records))
    {
        builder->fixup_records[builder->fixups] = (uint16_t)record;
        builder->fixup_targets[builder->fixups] = (uint16_t)target;
        builder->fixups += 1;
    }
    else
    {
        builder->valid = false;
    }
}

static uint32_t x86_zen5_file(uint32_t operand)
{
    const uint32_t klass = operand & BUSTER_X86_OPERAND_CLASS;
    uint32_t file = X86_COST_FILE_NONE;
    if (klass >= BUSTER_X86_CLASS_GPR8 && klass <= BUSTER_X86_CLASS_GPR64)
    {
        file = X86_COST_FILE_GPR;
    }
    else if (klass >= BUSTER_X86_CLASS_XMM && klass <= BUSTER_X86_CLASS_ZMM)
    {
        file = X86_COST_FILE_VECTOR;
    }
    else if (klass == BUSTER_X86_CLASS_OPMASK)
    {
        file = X86_COST_FILE_OPMASK;
    }
    else if (klass == BUSTER_X86_CLASS_ST)
    {
        file = X86_COST_FILE_X87;
    }
    return file;
}

// A floating-point operation's constant sources are 1.0 (multiplicative) or, for an addition or subtraction, 0.0, so
// that each chain keeps the value 1.0 and never reaches a denormal, an infinity or a NaN.
static void x86_zen5_fill_of(const X86Effects* effects, X86CostShape* shape)
{
    bool floating = false;
    switch (effects->operation)
    {
        case X86_OPERATION_FMOVE:
        case X86_OPERATION_FADD:
        case X86_OPERATION_FSUB:
        case X86_OPERATION_FMUL:
        case X86_OPERATION_FDIV:
        case X86_OPERATION_FMIN:
        case X86_OPERATION_FMAX:
        case X86_OPERATION_FSQRT:
        case X86_OPERATION_FROUND:
        case X86_OPERATION_FCOMPARE:
        case X86_OPERATION_FCOMPARE_SIGNALING:
        case X86_OPERATION_FCOMPARE_MASK:
        case X86_OPERATION_FLOAT_TO_INT:
        case X86_OPERATION_FLOAT_TO_FLOAT:
        case X86_OPERATION_FMA:
            floating = true;
            break;
        default:
            break;
    }
    shape->fill = !floating ? X86_COST_FILL_INTEGER : effects->width == 8 ? X86_COST_FILL_DOUBLE : X86_COST_FILL_FLOAT;
    shape->additive = effects->operation == X86_OPERATION_FADD || effects->operation == X86_OPERATION_FSUB || effects->operation == X86_OPERATION_FMA;
}

// The general registers a fixture may write: the twelve in x86_zen5_gpr_order but those the form names implicitly.
static void x86_zen5_pool(X86CostShape* shape)
{
    shape->pool_count = 0;
    for (uint32_t i = 0; i < BUSTER_X86_COST_POOL; i += 1)
    {
        if ((shape->fixed & (1u << x86_zen5_gpr_order[i])) == 0)
        {
            shape->pool[shape->pool_count] = x86_zen5_gpr_order[i];
            shape->pool_count += 1;
        }
    }
}

// The largest ring that divides the block and fits `available` registers.
static uint32_t x86_zen5_ring(uint32_t available)
{
    static const uint8_t sizes[] = { 12, 10, 8, 6, 5, 4, 3, 2, 1 };
    uint32_t ring = 1;
    for (uint32_t i = BUSTER_ARRAY_LENGTH(sizes); i > 0; i -= 1)
    {
        ring = sizes[i - 1] <= available ? sizes[i - 1] : ring;
    }
    return ring;
}

static uint32_t x86_zen5_special_of(uint32_t form, const X86Effects* effects)
{
    const uint32_t control = x86_effects_control(effects);
    uint32_t special = X86_COST_GENERIC;
    if (control == BUSTER_X86_CONTROL_TRAP || effects->operation == X86_OPERATION_TRAP)
    {
        special = X86_COST_TRAP;
    }
    else if (control != BUSTER_X86_CONTROL_NONE)
    {
        special = X86_COST_CONTROL;
    }
    else if (effects->memory & (BUSTER_X86_MEMORY_EFFECT_STACK_READ | BUSTER_X86_MEMORY_EFFECT_STACK_WRITE))
    {
        special = X86_COST_STACK;
    }
    else if (effects->properties & (BUSTER_X86_EFFECT_X87 | BUSTER_X86_EFFECT_X87_CONTROL))
    {
        special = X86_COST_X87;
    }
    else if (x86_form_descriptions[form].rm & BUSTER_X86_OPERAND_VSIB)
    {
        special = X86_COST_GATHER;
    }
    else if (effects->implicit_writes != 0)
    {
        special = X86_COST_IMPLICIT;
    }
    return special;
}

// The immediate a fixture uses: 3 for arithmetic, 1 for a shift or rotate's count (a count of 3 shifts a chain's bits
// out as fast); a control immediate (a shuffle's, a compare's predicate) 0, which is valid for every form that takes
// one. docs/x86-zen5.md has the sweep showing that the value doesn't change a case's cost.
static int32_t x86_zen5_immediate(const X86CostShape* shape)
{
    const uint32_t kind = x86_effects_immediate_kind(shape->effects);
    int32_t immediate = 0;
    if (shape->description->immediate_bytes != 0)
    {
        immediate = kind == BUSTER_X86_IMMEDIATE_COUNT ? 1 : kind == BUSTER_X86_IMMEDIATE_VALUE ? 3 : 0;
    }
    return immediate;
}

// measure-x86-zen5's --immediate and --decorations: a fixture's immediate and EVEX decorations in place of its own, for
// the sweep that shows a case's cost doesn't depend on them (docs/x86-zen5.md).
static int64_t x86_zen5_override_immediate = INT64_MIN;
static int32_t x86_zen5_override_decorations = -1;

// Everything a case's fixtures need, from its form's description and effects; `shape->experiments` says which
// experiments its records allow (none for a form outside the effects tables).
static void x86_zen5_shape(uint32_t form, uint32_t operands, X86CostShape* shape)
{
    memset(shape, 0, sizeof(*shape));
    const X86FormDescription* description = &x86_form_descriptions[form];
    const X86Effects* effects = x86_effects(form);
    shape->description = description;
    shape->effects = effects;
    shape->form = form;
    shape->operands = operands;
    shape->destination = BUSTER_X86_COST_NO_ROLE;
    shape->second = BUSTER_X86_COST_NO_ROLE;
    const uint16_t operand[BUSTER_X86_COST_ROLES] = { description->reg, description->rm, description->vvvv };
    shape->memory = operands != X86_COST_REGISTER && operands != X86_COST_ZERO && operands != X86_COST_MASKED;
    shape->masked = operands == X86_COST_MASKED || operands == X86_COST_MASKED_MEMORY;
    const bool into = operands == X86_COST_MASKED_MEMORY && description->reg != BUSTER_X86_OPERAND_NONE &&
                      x86_zen5_file(description->reg) == X86_COST_FILE_VECTOR && (x86_effects_role(effects, BUSTER_X86_ROLE_REG) & BUSTER_X86_ROLE_WRITE) != 0;
    shape->decorations = (uint8_t)(shape->masked ? BUSTER_X86_COST_OPMASK_MERGE | (into ? BUSTER_X86_DECORATION_ZEROING : 0) : 0);
    // Whether the opmask makes the destination a source: the effects of a masked record of distinct registers, with
    // its opmasks (without them a masked record reads everything), say: merging does, zeroing doesn't. The destination's
    // chain then measures whether the processor waits for it (a merging vpaddd does; vpblendm's selection doesn't).
    bool merged = false;
    if (shape->masked && description->reg != BUSTER_X86_OPERAND_NONE && x86_zen5_file(description->reg) == X86_COST_FILE_VECTOR)
    {
        const X86Instruction sample = { .form = (uint16_t)form, .reg = 1, .vvvv = 2, .rm = 3, .decorations = shape->decorations };
        uint64_t reads = 0;
        uint64_t writes = 0;
        uint16_t opmasks = 0;
        x86_instructions_effects_opmasks_scalar(&sample, 1, &reads, &writes, &opmasks);
        merged = (reads & BUSTER_X86_RESOURCE_VECTOR(1)) != 0;
    }
    shape->lea = effects->operation == X86_OPERATION_LEA;
    shape->is4 = description->syntax >= X86_SYNTAX_REG_VVVV_RM_IS4 && description->syntax <= X86_SYNTAX_REG_VVVV_IS4_RM_IMM;
    for (uint32_t role = 0; role < BUSTER_X86_COST_ROLES; role += 1)
    {
        const uint32_t bits = x86_effects_role(effects, role);
        const bool memory = role == BUSTER_X86_ROLE_RM && shape->memory;
        shape->files[role] = memory ? X86_COST_FILE_NONE : (uint8_t)x86_zen5_file(operand[role]);
        // A register write that keeps the register's other bits (an 8- or 16-bit one, SSE's scalar ones) reads it.
        const uint32_t rest = (bits & BUSTER_X86_ROLE_REST) >> BUSTER_X86_ROLE_REST_SHIFT;
        const bool merges = (bits & BUSTER_X86_ROLE_WRITE) && !memory && (rest == BUSTER_X86_REST_MERGE || rest == BUSTER_X86_REST_MERGE_IF_REGISTER);
        const bool keeps = merged && role == BUSTER_X86_ROLE_REG && (bits & BUSTER_X86_ROLE_WRITE) && !memory;
        shape->access[role] = (uint8_t)((bits & (BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE)) | (merges || keeps ? BUSTER_X86_ROLE_READ : 0));
        if (memory)
        {
            shape->loads = (bits & BUSTER_X86_ROLE_READ) != 0 && !shape->lea;
            shape->stores = (bits & BUSTER_X86_ROLE_WRITE) != 0;
        }
        else if ((bits & BUSTER_X86_ROLE_WRITE) && shape->files[role] != X86_COST_FILE_NONE)
        {
            if (shape->destination == BUSTER_X86_COST_NO_ROLE)
            {
                shape->destination = (uint8_t)role;
            }
            else
            {
                shape->second = (uint8_t)role;
            }
        }
    }
    shape->flags_written = effects->flags_written & BUSTER_X86_FLAGS_STATUS;
    shape->flags_read = effects->flags_read & BUSTER_X86_FLAGS_STATUS;
    shape->fixed = effects->implicit_reads | effects->implicit_writes;
    shape->special = (uint8_t)x86_zen5_special_of(form, effects);
    // An indirect call or jump reaches its target through R15 (BUSTER_X86_COST_ADDRESS), which no pool register may be.
    const bool indirect = shape->special == X86_COST_CONTROL && description->relative_bytes == 0 &&
                          (effects->operation == X86_OPERATION_CALL || effects->operation == X86_OPERATION_JMP);
    shape->fixed |= indirect ? 1u << X86_REGISTER_R15 : 0;
    x86_zen5_fill_of(effects, shape);
    x86_zen5_pool(shape);
    shape->immediate = x86_zen5_override_immediate != INT64_MIN ? (int32_t)x86_zen5_override_immediate : x86_zen5_immediate(shape);
    uint32_t experiments = 0;
    if (effects->operation != X86_OPERATION_NONE && shape->special != X86_COST_TRAP)
    {
        experiments = (1u << X86_COST_INDEPENDENT) | (1u << X86_COST_INDEPENDENT_SPARE);
        for (uint32_t b = 0; b < BUSTER_X86_PIPE_BLOCKERS; b += 1)
        {
            // A blocker that writes the flags would feed a flag reader: those mixes measure a dependency, not a pipe.
            const bool feeds = x86_zen5_blockers[b].writes_flags && shape->flags_read != 0;
            experiments |= feeds ? 0 : 1u << (X86_COST_MIX_FIRST + b);
        }
    }
    if (shape->special == X86_COST_GENERIC && experiments != 0)
    {
        const uint32_t destination = shape->destination;
        const bool result = destination != BUSTER_X86_COST_NO_ROLE;
        const uint32_t result_file = result ? shape->files[destination] : X86_COST_FILE_NONE;
        // A destination the form reads too (add, a merge) chains through itself; a read-modify-write memory operand
        // chains through the memory.
        if ((result && (shape->access[destination] & BUSTER_X86_ROLE_READ) && shape->operands != X86_COST_ZERO) || (shape->loads && shape->stores))
        {
            experiments |= 1u << X86_COST_CHAIN_DESTINATION;
        }
        for (uint32_t role = 0; result && shape->operands != X86_COST_ZERO && role < BUSTER_X86_COST_ROLES; role += 1)
        {
            const uint32_t file = shape->files[role];
            const bool source = role != destination && role != shape->second && (shape->access[role] & BUSTER_X86_ROLE_READ) && file != X86_COST_FILE_NONE;
            const bool bridged = file == result_file || ((file == X86_COST_FILE_GPR || file == X86_COST_FILE_VECTOR || file == X86_COST_FILE_OPMASK) &&
                                                         (result_file == X86_COST_FILE_GPR || result_file == X86_COST_FILE_VECTOR || result_file == X86_COST_FILE_OPMASK));
            experiments |= source && bridged ? 1u << (X86_COST_CHAIN_REG + role) : 0;
        }
        // LEA's address registers are its sources: the base by CHAIN_RM, the index by CHAIN_INDEX.
        if (shape->lea)
        {
            experiments |= 1u << X86_COST_CHAIN_RM;
            experiments |= operands != X86_COST_LEA_BASE ? 1u << X86_COST_CHAIN_INDEX : 0;
        }
        // The address chain needs a register or flags result to feed back into the base.
        const bool feedable = (result && (result_file == X86_COST_FILE_GPR || result_file == X86_COST_FILE_VECTOR || result_file == X86_COST_FILE_OPMASK)) ||
                              (!result && shape->flags_written != 0);
        if (shape->memory && shape->loads && !shape->stores && feedable)
        {
            experiments |= 1u << X86_COST_CHAIN_ADDRESS;
        }
        // The flags' chain: from a general or vector register source through the flags and a cmov back into it.
        uint32_t flags_source = BUSTER_X86_COST_NO_ROLE;
        for (uint32_t role = 0; role < BUSTER_X86_COST_ROLES && flags_source == BUSTER_X86_COST_NO_ROLE; role += 1)
        {
            const uint32_t file = shape->files[role];
            flags_source = (shape->access[role] & BUSTER_X86_ROLE_READ) && (file == X86_COST_FILE_GPR || file == X86_COST_FILE_VECTOR) ? role : flags_source;
        }
        if (shape->flags_written != 0 && flags_source != BUSTER_X86_COST_NO_ROLE && shape->operands != X86_COST_ZERO && !shape->stores)
        {
            experiments |= 1u << X86_COST_CHAIN_FLAGS;
        }
    }
    else if (shape->special == X86_COST_IMPLICIT)
    {
        experiments |= 1u << X86_COST_CHAIN_DESTINATION;
    }
    else if (shape->special == X86_COST_X87)
    {
        // The x87 arithmetic and fcmov chain through their stack register; loads, stores and control don't.
        const uint32_t operation = effects->operation;
        experiments |= operation == X86_OPERATION_X87_ARITHMETIC || operation == X86_OPERATION_X87_CMOVCC ? 1u << X86_COST_CHAIN_DESTINATION : 0;
    }
    else if (shape->special == X86_COST_GATHER)
    {
        experiments |= 1u << X86_COST_CHAIN_INDEX;
    }
    shape->experiments = experiments;
}

// The register of `file` in pool slot `slot`: the general pool's, vector registers 0 to 11, opmasks 1 to 5.
static uint8_t x86_zen5_slot(const X86CostShape* shape, uint32_t file, uint32_t slot)
{
    uint8_t number = 0;
    if (file == X86_COST_FILE_GPR)
    {
        number = shape->pool[slot % shape->pool_count];
    }
    else if (file == X86_COST_FILE_VECTOR)
    {
        number = (uint8_t)(slot % BUSTER_X86_COST_POOL);
    }
    else if (file == X86_COST_FILE_OPMASK)
    {
        number = (uint8_t)(1 + slot % BUSTER_X86_COST_OPMASKS);
    }
    return number;
}

// A constant source of `file` in role `role`: no fixture writes it.
static uint8_t x86_zen5_constant(uint32_t file, uint32_t role)
{
    uint8_t number = 0;
    if (file == X86_COST_FILE_GPR)
    {
        number = BUSTER_X86_COST_CONSTANT;
    }
    else if (file == X86_COST_FILE_VECTOR)
    {
        number = role == BUSTER_X86_ROLE_VVVV ? BUSTER_X86_COST_VECTOR_VVVV : role == BUSTER_X86_ROLE_RM ? BUSTER_X86_COST_VECTOR_RM : BUSTER_X86_COST_VECTOR_IS4;
    }
    else if (file == X86_COST_FILE_OPMASK)
    {
        number = role == BUSTER_X86_ROLE_RM ? BUSTER_X86_COST_OPMASK_RM : BUSTER_X86_COST_OPMASK_VVVV;
    }
    return number;
}

// The general registers a chained base or index may be: the pool's but RBP and R13, whose zero displacement the
// encoding forces, which would change an address's kind.
static uint8_t x86_zen5_base(const X86CostShape* shape, uint32_t slot)
{
    uint8_t bases[BUSTER_X86_COST_POOL];
    uint32_t count = 0;
    for (uint32_t i = 0; i < shape->pool_count; i += 1)
    {
        const uint8_t number = shape->pool[i];
        if (number != X86_REGISTER_RBP && number != X86_REGISTER_R13)
        {
            bases[count] = number;
            count += 1;
        }
    }
    return count != 0 ? bases[slot % count] : BUSTER_X86_COST_CONSTANT;
}

// The memory operand of a case's kind: [base + displacement] for MEMORY, [base + R14 * 8 + displacement] for
// MEMORY_INDEX, and LEA's shapes, whose base and index are `base` and `index`.
static void x86_zen5_address(const X86CostShape* shape, X86Instruction* record, uint8_t base, uint8_t index, int32_t displacement)
{
    record->rm = base;
    switch (shape->operands)
    {
        case X86_COST_MEMORY:
        case X86_COST_MASKED_MEMORY:
            record->mode = BUSTER_X86_MEMORY_BASE;
            record->displacement = displacement;
            break;
        case X86_COST_MEMORY_INDEX:
            record->mode = BUSTER_X86_MEMORY_BASE_INDEX(3);
            record->index = BUSTER_X86_COST_CONSTANT;
            record->displacement = displacement;
            break;
        case X86_COST_LEA_BASE:
            record->mode = BUSTER_X86_MEMORY_BASE;
            record->displacement = 8;
            break;
        case X86_COST_LEA_INDEX:
            record->mode = BUSTER_X86_MEMORY_BASE_INDEX(0);
            record->index = index;
            break;
        case X86_COST_LEA_SCALED:
            record->mode = BUSTER_X86_MEMORY_BASE_INDEX(3);
            record->index = index;
            break;
        default:
            record->mode = BUSTER_X86_MEMORY_BASE_INDEX(3);
            record->index = index;
            record->displacement = 4660;
            break;
    }
}

// A generic case's target record: its destination in pool slot `destination` (a second written role, xchg's or
// xadd's, in slot `second`), the role `chained` reading register `chained_register` (or BUSTER_X86_COST_NO_ROLE),
// every other register source a constant, and the memory operand at `base` (+ `displacement`; LEA's index `index`).
static X86Instruction x86_zen5_record(const X86CostShape* shape, uint32_t destination, uint32_t second, uint32_t chained, uint8_t chained_register, uint8_t base, uint8_t index,
                                      int32_t displacement)
{
    X86Instruction record = { .form = (uint16_t)shape->form, .immediate = shape->immediate };
    uint8_t* fields[BUSTER_X86_COST_ROLES] = { &record.reg, &record.rm, &record.vvvv };
    const uint8_t result = shape->destination != BUSTER_X86_COST_NO_ROLE ? x86_zen5_slot(shape, shape->files[shape->destination], destination) : 0;
    for (uint32_t role = 0; role < BUSTER_X86_COST_ROLES; role += 1)
    {
        const uint32_t file = shape->files[role];
        if (role == BUSTER_X86_ROLE_RM && shape->memory)
        {
            x86_zen5_address(shape, &record, base, index, displacement);
        }
        else if (file == X86_COST_FILE_NONE)
        {
            *fields[role] = 0;
        }
        else if (role == shape->destination)
        {
            *fields[role] = result;
        }
        else if (role == shape->second)
        {
            *fields[role] = x86_zen5_slot(shape, file, second);
        }
        else if (shape->operands == X86_COST_ZERO)
        {
            // A zero idiom's sources are its destination.
            *fields[role] = result;
        }
        else if (role == chained)
        {
            *fields[role] = chained_register;
        }
        else
        {
            *fields[role] = x86_zen5_constant(file, role);
        }
    }
    if (shape->is4)
    {
        record.immediate = BUSTER_X86_COST_VECTOR_IS4 << 4;
    }
    record.decorations = x86_zen5_override_decorations >= 0 ? (uint8_t)x86_zen5_override_decorations : shape->decorations;
    return record;
}

// The condition code of a jcc, setcc or cmovcc form: the low nibble of its last opcode byte.
static uint32_t x86_zen5_condition(uint32_t form)
{
    return x86_form_descriptions[form].opcode & 15;
}

// A cmov (64 bits) of condition `condition`: one that reads a flag in `flags`, CF, ZF, SF, OF or PF in that order.
static uint16_t x86_zen5_cmov(uint32_t flags)
{
    uint16_t form = X86_FORM_CMOVP_R64_RM64;
    form = (flags & BUSTER_X86_FLAG_OF) ? X86_FORM_CMOVO_R64_RM64 : form;
    form = (flags & BUSTER_X86_FLAG_SF) ? X86_FORM_CMOVS_R64_RM64 : form;
    form = (flags & BUSTER_X86_FLAG_ZF) ? X86_FORM_CMOVE_R64_RM64 : form;
    form = (flags & BUSTER_X86_FLAG_CF) ? X86_FORM_CMOVB_R64_RM64 : form;
    return form;
}

// Records that move register `from` of file `source` into register `to` of file `target` (a general register
// `temporary` between an opmask and a vector one). They are the bridges of a chain between files; the reduction
// subtracts their share of the round trips it measures (x86_zen5_references).
static void x86_zen5_bridge(X86CostBuilder* builder, uint32_t source, uint8_t from, uint32_t target, uint8_t to, uint8_t temporary)
{
    if (source == X86_COST_FILE_VECTOR && target == X86_COST_FILE_GPR)
    {
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_MOVQ_RM64_X, .rm = to, .reg = from });
    }
    else if (source == X86_COST_FILE_GPR && target == X86_COST_FILE_VECTOR)
    {
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_MOVQ_X_RM64, .reg = to, .rm = from });
    }
    else if (source == X86_COST_FILE_OPMASK && target == X86_COST_FILE_GPR)
    {
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_KMOVQ_R64_K, .reg = to, .rm = from });
    }
    else if (source == X86_COST_FILE_GPR && target == X86_COST_FILE_OPMASK)
    {
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_KMOVQ_K_R64, .reg = to, .rm = from });
    }
    else if (source == X86_COST_FILE_OPMASK && target == X86_COST_FILE_VECTOR)
    {
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_KMOVQ_R64_K, .reg = temporary, .rm = from });
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_MOVQ_X_RM64, .reg = to, .rm = temporary });
    }
    else if (source == X86_COST_FILE_VECTOR && target == X86_COST_FILE_OPMASK)
    {
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_MOVQ_RM64_X, .rm = temporary, .reg = from });
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_KMOVQ_K_R64, .reg = to, .rm = temporary });
    }
    else if (source != target)
    {
        builder->valid = false;
    }
}

// The bridges' round trips (cycles per unit) the reduction divides between the directions, and the blockers alone.
typedef struct X86CostReferences X86CostReferences;
struct X86CostReferences
{
    double vector;
    double opmask;
    double flags;
    double blocker_cycles[BUSTER_X86_PIPE_BLOCKERS];
    double blocker_ops[BUSTER_X86_PIPE_BLOCKERS];
};

// The share of `from` -> `to`'s bridge in cycles: half of its round trip, two halves through a general register.
static double x86_zen5_bridge_share(const X86CostReferences* references, uint32_t from, uint32_t to)
{
    double share = 0;
    share += from == X86_COST_FILE_VECTOR || to == X86_COST_FILE_VECTOR ? references->vector / 2 : 0;
    share += from == X86_COST_FILE_OPMASK || to == X86_COST_FILE_OPMASK ? references->opmask / 2 : 0;
    return from == to ? 0 : share;
}

// The distance between a case's data slots: its memory operand's size, at least 8 bytes, a power of two.
static uint32_t x86_zen5_stride(const X86CostShape* shape)
{
    uint32_t stride = 8;
    while (stride < shape->description->operand_bytes && stride < 64)
    {
        stride *= 2;
    }
    return stride;
}

// Unit `i`'s data slot: consecutive slots of the operand's size, as a function's spills to adjacent stack slots are, so
// that stores and read-modify-writes stay independent and stores share lines (independent stores to a line each are
// limited by the lines' commits, a different cost).
static int32_t x86_zen5_line(const X86CostShape* shape, uint32_t i)
{
    const uint32_t stride = x86_zen5_stride(shape);
    const uint32_t slots = BUSTER_X86_COST_DATA_TARGETS / stride < BUSTER_X86_COST_DATA_SLOTS ? BUSTER_X86_COST_DATA_TARGETS / stride : BUSTER_X86_COST_DATA_SLOTS;
    return (int32_t)(stride * (i % slots));
}

// The data's constants (BUSTER_X86_COST_DATA_CONSTANTS +): the registers' initial pattern, the constant sources'
// pattern, an x87 long double 1.0, the dividend, and an x87 control word.
#define BUSTER_X86_COST_CONSTANT_INITIAL 0
#define BUSTER_X86_COST_CONSTANT_SOURCE 64
#define BUSTER_X86_COST_CONSTANT_DIVIDEND 128
#define BUSTER_X86_COST_CONSTANT_OPMASKS 192

// A generic case's unit `i` of `experiment`.
static void x86_zen5_generic_unit(const X86CostShape* shape, X86CostExperiment experiment, uint32_t i, X86CostBuilder* builder)
{
    const uint32_t destination = shape->destination;
    const uint32_t result_file = destination != BUSTER_X86_COST_NO_ROLE ? shape->files[destination] : X86_COST_FILE_NONE;
    // Twelve independent instances (as many as the file's pool has), or one fewer register's worth where a mix's blockers
    // write the pool's last register (the spare experiment and the mixes).
    const uint32_t available = result_file == X86_COST_FILE_GPR ? shape->pool_count : result_file == X86_COST_FILE_OPMASK ? BUSTER_X86_COST_OPMASKS : BUSTER_X86_COST_POOL;
    const uint32_t ring = x86_zen5_ring(experiment == X86_COST_INDEPENDENT || result_file == X86_COST_FILE_OPMASK ? available : available - 1);
    // Xchg and xadd write a second register: the ring's second half.
    const uint32_t half = shape->second != BUSTER_X86_COST_NO_ROLE ? ring / 2 : ring;
    uint32_t slot = i % half;
    uint32_t second = half + i % half;
    uint32_t chained = BUSTER_X86_COST_NO_ROLE;
    uint8_t chained_register = 0;
    uint8_t base = BUSTER_X86_COST_DATA;
    uint8_t index = BUSTER_X86_COST_CONSTANT;
    int32_t displacement = x86_zen5_line(shape, i);
    if (experiment == X86_COST_CHAIN_DESTINATION)
    {
        slot = 0;
        second = 6;
        displacement = 0;
    }
    else if (experiment >= X86_COST_CHAIN_REG && experiment <= X86_COST_CHAIN_VVVV && !shape->lea)
    {
        // A ring of twelve: the unit writes slot i + 1 and reads, in the chained role, what unit i - 1 wrote (or its
        // bridge into the role's file).
        const uint32_t role = experiment - X86_COST_CHAIN_REG;
        chained = role;
        slot = (i + 1) % 12;
        chained_register = x86_zen5_slot(shape, shape->files[role], i % 12);
        displacement = 0;
    }
    else if (shape->lea && (experiment == X86_COST_CHAIN_RM || experiment == X86_COST_CHAIN_INDEX))
    {
        // LEA's base or index chain, through registers that never force a displacement.
        const uint8_t previous = x86_zen5_base(shape, i % 8);
        const uint8_t next = x86_zen5_base(shape, (i + 1) % 8);
        base = experiment == X86_COST_CHAIN_RM ? previous : BUSTER_X86_COST_CONSTANT;
        index = experiment == X86_COST_CHAIN_INDEX ? previous : BUSTER_X86_COST_CONSTANT;
        slot = 0;
        for (uint32_t s = 0; s < shape->pool_count; s += 1)
        {
            slot = shape->pool[s] == next ? s : slot;
        }
    }
    else if (experiment == X86_COST_CHAIN_ADDRESS)
    {
        // The base is slot 1's register, initialized to the data; the result is slot 0's.
        base = x86_zen5_base(shape, 1);
        slot = 0;
        displacement = 0;
        if (shape->pool[0] == base)
        {
            builder->valid = false;
        }
    }
    else if (experiment == X86_COST_CHAIN_FLAGS)
    {
        // The ring is slots 0 to 7, the result slot 8, the cmov's general register 9 and its vector value 10.
        slot = 8;
        displacement = 0;
    }
    if (experiment == X86_COST_CHAIN_FLAGS)
    {
        // The flags' source in a ring of eight general (or vector) registers: the unit reads ring i, writes the flags,
        // and a cmov writes ring i + 1 (through a general register and a bridge, for a vector source).
        uint32_t source = BUSTER_X86_COST_NO_ROLE;
        for (uint32_t role = 0; role < BUSTER_X86_COST_ROLES && source == BUSTER_X86_COST_NO_ROLE; role += 1)
        {
            const uint32_t file = shape->files[role];
            source = (shape->access[role] & BUSTER_X86_ROLE_READ) && (file == X86_COST_FILE_GPR || file == X86_COST_FILE_VECTOR) ? role : source;
        }
        const uint32_t file = source != BUSTER_X86_COST_NO_ROLE ? shape->files[source] : X86_COST_FILE_NONE;
        const uint8_t read = x86_zen5_slot(shape, file, i % 8);
        const uint8_t written = x86_zen5_slot(shape, file, (i + 1) % 8);
        X86Instruction record = x86_zen5_record(shape, slot, 9, source == destination ? BUSTER_X86_COST_NO_ROLE : source, read, base, index, displacement);
        if (source == destination)
        {
            uint8_t* fields[BUSTER_X86_COST_ROLES] = { &record.reg, &record.rm, &record.vvvv };
            *fields[source] = read;
        }
        x86_zen5_push(builder, record);
        // The cmov's source: R14 (3), or for a vector source slot 10's general register, which holds the fill's bits.
        const uint8_t gpr = file == X86_COST_FILE_GPR ? written : shape->pool[9 % shape->pool_count];
        const uint8_t value = file == X86_COST_FILE_GPR ? BUSTER_X86_COST_CONSTANT : shape->pool[10 % shape->pool_count];
        x86_zen5_helper(builder, (X86Instruction){ .form = x86_zen5_cmov(shape->flags_written), .reg = gpr, .rm = value });
        if (file == X86_COST_FILE_VECTOR)
        {
            x86_zen5_bridge(builder, X86_COST_FILE_GPR, gpr, X86_COST_FILE_VECTOR, written, 0);
        }
    }
    else
    {
        const X86Instruction record = x86_zen5_record(shape, slot, second, chained, chained_register, base, index, displacement);
        x86_zen5_push(builder, record);
        if (chained != BUSTER_X86_COST_NO_ROLE)
        {
            // Into the chained role's file, where the result's differs.
            const uint32_t file = shape->files[chained];
            const uint8_t written = x86_zen5_slot(shape, result_file, slot);
            const uint8_t next = x86_zen5_slot(shape, file, (i + 1) % 12);
            x86_zen5_bridge(builder, result_file, written, file, next, shape->pool[shape->pool_count - 1]);
        }
        else if (experiment == X86_COST_CHAIN_ADDRESS)
        {
            // The result into a general register, then and 0 and an add into the base: the next unit's address
            // waits for this unit's result. A flags result goes through a cmov.
            const uint8_t gpr = shape->pool[0] == base ? shape->pool[2] : shape->pool[0];
            if (result_file == X86_COST_FILE_NONE)
            {
                x86_zen5_helper(builder, (X86Instruction){ .form = x86_zen5_cmov(shape->flags_written), .reg = gpr, .rm = BUSTER_X86_COST_CONSTANT });
            }
            else
            {
                x86_zen5_bridge(builder, result_file, x86_zen5_slot(shape, result_file, 0), X86_COST_FILE_GPR, gpr, 0);
            }
            x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_AND_RM64_IMM8, .rm = gpr, .immediate = 0 });
            x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_ADD_RM64_R64, .rm = base, .reg = gpr });
        }
    }
}

// Unit `i` of a form with an implicit result register: mul and imul of one operand, div, idiv, cqo, cdq, an
// accumulator's immediate, cmpxchg. The independent units first set the implicit registers by helpers (a move of 3 or
// of the dividend, a zero idiom of RDX), so that no unit waits for another; the chain runs through RAX, restoring a
// dividend's magnitude with an or and RDX with a zero idiom.
static void x86_zen5_implicit_unit(const X86CostShape* shape, X86CostExperiment experiment, uint32_t i, X86CostBuilder* builder)
{
    const uint32_t operation = shape->effects->operation;
    const bool chain = experiment == X86_COST_CHAIN_DESTINATION;
    X86Instruction record = { .form = (uint16_t)shape->form, .immediate = shape->immediate };
    if (shape->memory)
    {
        x86_zen5_address(shape, &record, BUSTER_X86_COST_DATA, BUSTER_X86_COST_CONSTANT, chain ? 0 : x86_zen5_line(shape, i));
    }
    else if (shape->description->rm != BUSTER_X86_OPERAND_NONE)
    {
        record.rm = BUSTER_X86_COST_CONSTANT;
    }
    record.reg = shape->description->reg != BUSTER_X86_OPERAND_NONE ? BUSTER_X86_COST_CONSTANT : 0;
    const X86Instruction dividend = { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = BUSTER_X86_COST_DATA, .mode = BUSTER_X86_MEMORY_BASE,
                                      .displacement = BUSTER_X86_COST_DATA_CONSTANTS + BUSTER_X86_COST_CONSTANT_DIVIDEND };
    const X86Instruction zero = { .form = X86_FORM_XOR_R32_RM32, .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RDX };
    const bool divides = operation == X86_OPERATION_DIV || operation == X86_OPERATION_IDIV;
    if (!chain && divides)
    {
        x86_zen5_helper(builder, dividend);
    }
    else if (!chain && operation == X86_OPERATION_CMPXCHG)
    {
        // The comparand equal to the memory's 3: each exchange succeeds.
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = BUSTER_X86_COST_CONSTANT });
    }
    else if (!chain && operation != X86_OPERATION_SIGN_SPLIT)
    {
        // A zero idiom of EAX: a move into RAX can retire with an accumulator's operation as one macro-op (the
        // 9700X's first run counted `mov rax, r14` and `add rax, imm32` as one).
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_XOR_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX });
    }
    // RDX's zero idiom depends on nothing, so it breaks no chain: the dividend's high half is zero, and the quotient fits.
    if (divides)
    {
        x86_zen5_helper(builder, zero);
    }
    x86_zen5_push(builder, record);
    if (chain && divides)
    {
        X86Instruction restore = dividend;
        restore.form = X86_FORM_OR_R64_RM64;
        x86_zen5_helper(builder, restore);
    }
    else if (chain && operation == X86_OPERATION_SIGN_SPLIT)
    {
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_OR_R64_RM64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDX });
    }
}

// Unit `i` of push or pop: 240 pushes and one LEA giving the stack back at the body's end, or one LEA reserving it at
// the body's start and 240 pops.
static void x86_zen5_stack_unit(const X86CostShape* shape, X86CostExperiment experiment, uint32_t i, X86CostBuilder* builder)
{
    const bool push = shape->effects->operation == X86_OPERATION_PUSH;
    const uint32_t ring = x86_zen5_ring(experiment == X86_COST_INDEPENDENT ? shape->pool_count : shape->pool_count - 1);
    const X86Instruction reserve = { .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RSP, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE,
                                     .displacement = push ? 8 * BUSTER_X86_COST_BLOCK : -8 * BUSTER_X86_COST_BLOCK };
    if (!push && i == 0)
    {
        x86_zen5_helper(builder, reserve);
    }
    x86_zen5_push(builder, (X86Instruction){ .form = (uint16_t)shape->form, .rm = push ? BUSTER_X86_COST_CONSTANT : x86_zen5_slot(shape, X86_COST_FILE_GPR, i % ring) });
    if (push && i == BUSTER_X86_COST_BLOCK - 1)
    {
        x86_zen5_helper(builder, reserve);
    }
}

// The comparison of R14 (3) that leaves condition `condition` false, so that every jcc of it falls through:
// cmp r14d, imm32, whose flags each condition's imm32 below gives.
static int32_t x86_zen5_false_condition(uint32_t condition)
{
    static const int32_t immediates[16] = {
        3,          // O: 3 - 3 doesn't overflow.
        INT32_MIN,  // NO: 3 - INT32_MIN overflows 32 bits.
        3,          // B: CF clear.
        4,          // AE: 3 < 4, CF set.
        1,          // E: 2, ZF clear.
        3,          // NE: 0, ZF set.
        1,          // BE: CF and ZF clear.
        3,          // A: ZF set.
        1,          // S: 2, SF clear.
        4,          // NS: -1, SF set.
        2,          // P: 1, odd parity, PF clear.
        3,          // NP: 0, even parity, PF set.
        1,          // L: SF = OF = 0.
        4,          // GE: SF set, OF clear.
        1,          // LE: ZF clear, SF = OF.
        3,          // G: ZF set.
    };
    return immediates[condition & 15];
}

// The stub a call reaches (a ret past the epilogue), as a fixup target.
#define BUSTER_X86_COST_STUB 0xffff
// R15 holds the stub's address for an indirect call, and a jump's target for an indirect jump: those forms keep it out
// of the pool.
#define BUSTER_X86_COST_ADDRESS X86_REGISTER_R15

// Unit `i` of a control transfer: a jmp to the next record (taken), a jcc to it (not taken: the body starts with the
// comparison that makes its condition false), a call of the stub (whose ret returns to the next record), ret as the
// stub's after a call, and jmp through a register LEA sets to the next record.
static void x86_zen5_control_unit(const X86CostShape* shape, uint32_t i, X86CostBuilder* builder)
{
    const uint32_t operation = shape->effects->operation;
    const uint32_t record = builder->count;
    if (operation == X86_OPERATION_JCC)
    {
        if (i == 0)
        {
            x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_CMP_RM32_IMM32, .rm = BUSTER_X86_COST_CONSTANT, .immediate = x86_zen5_false_condition(x86_zen5_condition(shape->form)) });
        }
        x86_zen5_fixup(builder, builder->count, builder->count + 1);
        x86_zen5_push(builder, (X86Instruction){ .form = (uint16_t)shape->form });
    }
    else if (operation == X86_OPERATION_JMP && shape->description->relative_bytes != 0)
    {
        x86_zen5_fixup(builder, record, record + 1);
        x86_zen5_push(builder, (X86Instruction){ .form = (uint16_t)shape->form });
    }
    else if (operation == X86_OPERATION_JMP)
    {
        x86_zen5_fixup(builder, record, record + 2);
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = BUSTER_X86_COST_ADDRESS, .mode = BUSTER_X86_MEMORY_RIP });
        x86_zen5_push(builder, (X86Instruction){ .form = (uint16_t)shape->form, .rm = BUSTER_X86_COST_ADDRESS });
    }
    else if (operation == X86_OPERATION_CALL && shape->description->relative_bytes != 0)
    {
        x86_zen5_fixup(builder, record, BUSTER_X86_COST_STUB);
        x86_zen5_push(builder, (X86Instruction){ .form = (uint16_t)shape->form });
    }
    else if (operation == X86_OPERATION_CALL)
    {
        x86_zen5_push(builder, (X86Instruction){ .form = (uint16_t)shape->form, .rm = BUSTER_X86_COST_ADDRESS });
    }
    else if (operation == X86_OPERATION_RET && shape->description->immediate_bytes == 0)
    {
        x86_zen5_fixup(builder, record, BUSTER_X86_COST_STUB);
        x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_CALL_REL32 });
    }
    else
    {
        builder->valid = false;
    }
}

// Unit `i` of an x87 form. The stack holds five values of 1.0 (st0 to st4) and the constant c (st5): 0.0 for an
// addition or subtraction, 1.0 otherwise. A form that pushes runs with an fstp st0 that pops it again, one that pops
// with an fld that pushes first, and one that works on st0 alone with an fxch that rotates the values (helpers).
static void x86_zen5_x87_unit(const X86CostShape* shape, X86CostExperiment experiment, uint32_t i, X86CostBuilder* builder)
{
    const bool chain = experiment == X86_COST_CHAIN_DESTINATION;
    const uint32_t k = chain ? 0 : i % 5;
    const uint32_t form = shape->form;
    X86Instruction record = { .form = (uint16_t)form };
    if (shape->memory)
    {
        x86_zen5_address(shape, &record, BUSTER_X86_COST_DATA, BUSTER_X86_COST_CONSTANT, chain ? 0 : x86_zen5_line(shape, i));
    }
    const X86Instruction push = { .form = X86_FORM_FLD_ST, .rm = 0 };
    const X86Instruction pop = { .form = X86_FORM_FSTP_ST, .rm = 0 };
    const X86Instruction rotate = { .form = X86_FORM_FXCH_ST, .rm = (uint8_t)(1 + i % 4) };
    switch (form)
    {
        case X86_FORM_FADDP_ST_ST0:
        case X86_FORM_FSUBP_ST_ST0:
        case X86_FORM_FMULP_ST_ST0:
        case X86_FORM_FDIVP_ST_ST0:
            // A copy of c on top, then x_k = x_k op c and the pop.
            x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_FLD_ST, .rm = 5 });
            record.rm = (uint8_t)(k + 1);
            x86_zen5_push(builder, record);
            break;
        case X86_FORM_FADD_M32:
        case X86_FORM_FCHS:
            if (!chain)
            {
                x86_zen5_helper(builder, rotate);
            }
            x86_zen5_push(builder, record);
            break;
        case X86_FORM_FCMOVBE_ST0_ST:
            if (!chain)
            {
                x86_zen5_helper(builder, rotate);
            }
            record.rm = 5;
            x86_zen5_push(builder, record);
            break;
        case X86_FORM_FLD_ST:
            record.rm = (uint8_t)k;
            x86_zen5_push(builder, record);
            x86_zen5_helper(builder, pop);
            break;
        case X86_FORM_FLDZ:
        case X86_FORM_FLD_M32:
        case X86_FORM_FLD_M64:
        case X86_FORM_FLD_M80:
        case X86_FORM_FILD_M32:
        case X86_FORM_FILD_M64:
            x86_zen5_push(builder, record);
            x86_zen5_helper(builder, pop);
            break;
        case X86_FORM_FSTP_ST:
            x86_zen5_helper(builder, push);
            record.rm = (uint8_t)(k + 1);
            x86_zen5_push(builder, record);
            break;
        case X86_FORM_FSTP_M32:
        case X86_FORM_FSTP_M64:
        case X86_FORM_FSTP_M80:
        case X86_FORM_FISTP_M64:
            x86_zen5_helper(builder, push);
            x86_zen5_push(builder, record);
            break;
        case X86_FORM_FUCOMI_ST0_ST:
            record.rm = (uint8_t)(k + 1);
            x86_zen5_push(builder, record);
            break;
        case X86_FORM_FUCOMIP_ST0_ST:
            x86_zen5_helper(builder, push);
            record.rm = (uint8_t)(k + 1);
            x86_zen5_push(builder, record);
            break;
        case X86_FORM_FLDCW_M16:
        case X86_FORM_FNSTCW_M16:
            x86_zen5_push(builder, record);
            break;
        default:
            builder->valid = false;
            break;
    }
}

// Unit `i` of a gather: kxnorw sets the unit's mask (the gather clears it), then the gather of its elements at
// [rsi + index * element] into the unit's destination (the element's bytes the scale, so that each reads a whole slot).
// The chain runs through the index: the next unit's index is this unit's result, whose elements (the data's 3s, or 0s,
// x86_zen5_fill) stay valid offsets.
static void x86_zen5_gather_unit(const X86CostShape* shape, X86CostExperiment experiment, uint32_t i, X86CostBuilder* builder)
{
    const bool chain = experiment == X86_COST_CHAIN_INDEX;
    const uint32_t ring = x86_zen5_ring(experiment == X86_COST_INDEPENDENT ? BUSTER_X86_COST_POOL : BUSTER_X86_COST_POOL - 1);
    const uint8_t mask = (uint8_t)(1 + i % BUSTER_X86_COST_OPMASKS);
    const uint32_t scale = shape->effects->width == 8 ? 3 : 2;
    x86_zen5_helper(builder, (X86Instruction){ .form = X86_FORM_KXNORW_K_K_K, .reg = mask, .vvvv = mask, .rm = mask });
    X86Instruction record = { .form = (uint16_t)shape->form, .rm = BUSTER_X86_COST_DATA, .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale), .decorations = mask };
    record.reg = (uint8_t)(chain ? (i + 1) % 12 : i % ring);
    record.index = (uint8_t)(chain ? i % 12 : BUSTER_X86_COST_VECTOR_RM);
    x86_zen5_push(builder, record);
}

// Blocker `b`'s record after unit `i` of a mix: its sources constants, its destination the pool's last general register
// or vector register 12, which no target reads: a blocker only writes, so one register (renamed) serves them all.
static X86Instruction x86_zen5_blocker(const X86CostShape* shape, uint32_t b, uint32_t i)
{
    const uint8_t gpr = shape->pool[shape->pool_count - 1];
    const uint8_t vector = BUSTER_X86_COST_VECTOR_BLOCKER;
    const int32_t displacement = BUSTER_X86_COST_DATA_BLOCKER + (int32_t)(8 * (i % 32));
    X86Instruction record = { .form = x86_zen5_blockers[b].form };
    switch (b)
    {
        case 0:
            record.rm = gpr;
            record.immediate = 3;
            break;
        case 1:
        case 2:
            record.reg = gpr;
            record.rm = BUSTER_X86_COST_CONSTANT;
            record.immediate = b == 1 ? 3 : 1;
            break;
        case 3:
            record.reg = gpr;
            record.rm = BUSTER_X86_COST_DATA;
            record.mode = BUSTER_X86_MEMORY_BASE;
            record.displacement = displacement;
            break;
        case 4:
            record.reg = BUSTER_X86_COST_CONSTANT;
            record.rm = BUSTER_X86_COST_DATA;
            record.mode = BUSTER_X86_MEMORY_BASE;
            record.displacement = displacement;
            break;
        case 9:
            record.rm = gpr;
            record.reg = BUSTER_X86_COST_VECTOR_RM;
            break;
        default:
            record.reg = vector;
            record.vvvv = BUSTER_X86_COST_VECTOR_VVVV;
            record.rm = BUSTER_X86_COST_VECTOR_RM;
            break;
    }
    return record;
}

// The references a process measures before its cases: the bridges' round trips and each blocker alone.
typedef enum X86CostReference
{
    X86_COST_REFERENCE_NONE,
    // The loop alone, whose counts every sample's subtract.
    X86_COST_REFERENCE_EMPTY,
    // movq xmm, r64 then movq r64, xmm, chained through the general register.
    X86_COST_REFERENCE_VECTOR,
    // kmovq k, r64 then kmovq r64, k.
    X86_COST_REFERENCE_OPMASK,
    // test r64, r64 then cmovne r64, r14 (3, so the condition holds and the value stays).
    X86_COST_REFERENCE_FLAGS,
    X86_COST_REFERENCE_BLOCKER,
    X86_COST_REFERENCE_COUNT = X86_COST_REFERENCE_BLOCKER + BUSTER_X86_PIPE_BLOCKERS,
} X86CostReference;

// Which host features the fixtures' wrapper may use: the widest vector moves to set every register, and the opmasks'.
typedef struct X86CostHost X86CostHost;
struct X86CostHost
{
    uint8_t allowed[X86_FORM_COUNT];
    bool avx;
    bool avx512;
};

static void x86_zen5_host(X86CostHost* host)
{
    const X86Target target = x86_target_native();
    x86_target_forms(&target, host->allowed);
    host->avx = host->allowed[X86_FORM_VMOVDQU_Y_YM256] != 0;
    host->avx512 = host->allowed[X86_FORM_VMOVDQU64_Z_ZM512] != 0 && host->allowed[X86_FORM_KMOVQ_K_M64] != 0;
}

// The case's body: BUSTER_X86_COST_BLOCK units of `experiment` (or of `reference`), each mix's with its blocker after.
static void x86_zen5_body(const X86CostShape* shape, X86CostExperiment experiment, uint32_t reference, X86CostBuilder* builder)
{
    const uint8_t gpr = shape->pool[0];
    for (uint32_t i = 0; reference != X86_COST_REFERENCE_EMPTY && i < BUSTER_X86_COST_BLOCK; i += 1)
    {
        if (reference == X86_COST_REFERENCE_VECTOR)
        {
            x86_zen5_push(builder, (X86Instruction){ .form = X86_FORM_MOVQ_X_RM64, .reg = 0, .rm = gpr });
            x86_zen5_push(builder, (X86Instruction){ .form = X86_FORM_MOVQ_RM64_X, .rm = gpr, .reg = 0 });
        }
        else if (reference == X86_COST_REFERENCE_OPMASK)
        {
            x86_zen5_push(builder, (X86Instruction){ .form = X86_FORM_KMOVQ_K_R64, .reg = 1, .rm = gpr });
            x86_zen5_push(builder, (X86Instruction){ .form = X86_FORM_KMOVQ_R64_K, .reg = gpr, .rm = 1 });
        }
        else if (reference == X86_COST_REFERENCE_FLAGS)
        {
            x86_zen5_push(builder, (X86Instruction){ .form = X86_FORM_TEST_RM64_R64, .rm = gpr, .reg = gpr });
            x86_zen5_push(builder, (X86Instruction){ .form = X86_FORM_CMOVNE_R64_RM64, .reg = gpr, .rm = BUSTER_X86_COST_CONSTANT });
        }
        else if (reference >= X86_COST_REFERENCE_BLOCKER)
        {
            x86_zen5_push(builder, x86_zen5_blocker(shape, reference - X86_COST_REFERENCE_BLOCKER, i));
        }
        else
        {
            switch (shape->special)
            {
                case X86_COST_IMPLICIT:
                    x86_zen5_implicit_unit(shape, experiment, i, builder);
                    break;
                case X86_COST_STACK:
                    x86_zen5_stack_unit(shape, experiment, i, builder);
                    break;
                case X86_COST_CONTROL:
                    x86_zen5_control_unit(shape, i, builder);
                    break;
                case X86_COST_X87:
                    x86_zen5_x87_unit(shape, experiment, i, builder);
                    break;
                case X86_COST_GATHER:
                    x86_zen5_gather_unit(shape, experiment, i, builder);
                    break;
                default:
                    x86_zen5_generic_unit(shape, experiment, i, builder);
                    break;
            }
            if (experiment >= X86_COST_MIX_FIRST)
            {
                x86_zen5_push_kind(builder, x86_zen5_blocker(shape, experiment - X86_COST_MIX_FIRST, i), BUSTER_X86_COST_KIND_BLOCKER);
            }
        }
    }
}

// The bits a general register gets for a vector flags chain's cmov: 1.0 of the fill's element, or 3.
static int64_t x86_zen5_fill_bits(const X86CostShape* shape)
{
    int64_t bits = 3;
    bits = shape->fill == X86_COST_FILL_FLOAT ? INT64_C(0x3f8000003f800000) : bits;
    bits = shape->fill == X86_COST_FILL_DOUBLE ? INT64_C(0x3ff0000000000000) : bits;
    return bits;
}

// Builds case `shape`'s program for `experiment` (or a reference's, with a default shape): `void program(uint64_t
// iterations, void* data)` preserves the SysV callee-saved registers, sets every register it reads from the data's
// constants, aligns its loop to 64 bytes and returns with the x87 stack empty and the upper vector state clean. The
// mapping is writable only while it is built and executable only after both encoders agree on every byte.
static bool x86_zen5_program(const X86CostShape* shape, X86CostExperiment experiment, uint32_t reference, const X86CostHost* host, X86CostProgram* program)
{
    static const uint8_t saved[] = { X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_R12, X86_REGISTER_R13, X86_REGISTER_R14, X86_REGISTER_R15 };
    static X86CostBuilder builder;
    static uint32_t offsets[BUSTER_X86_COST_RECORDS + 1];
    static uint8_t vector_bytes[BUSTER_X86_COST_CODE_SIZE];
    memset(program, 0, sizeof(*program));
    builder.count = 0;
    builder.fixups = 0;
    builder.helpers = 0;
    builder.valid = reference != X86_COST_REFERENCE_NONE || (shape->experiments & (1u << experiment)) != 0;
    builder.valid &= shape->pool_count >= 7;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(saved); i += 1)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = saved[i] });
    }
    x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = BUSTER_X86_COST_COUNTER, .rm = X86_REGISTER_RDI });
    x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = BUSTER_X86_COST_CONSTANT, .immediate = 3 });
    for (uint32_t i = 0; i < BUSTER_X86_COST_POOL; i += 1)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = x86_zen5_gpr_order[i], .immediate = 3 });
    }
    const int64_t bits = x86_zen5_fill_bits(shape);
    x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_MOV_R64_IMM64, .rm = shape->pool[10 % shape->pool_count], .immediate = (int32_t)bits,
                                              .displacement = (int32_t)(bits >> 32) });
    if (experiment == X86_COST_CHAIN_ADDRESS && reference == X86_COST_REFERENCE_NONE)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = x86_zen5_base(shape, 1), .rm = BUSTER_X86_COST_DATA });
    }
    if (shape->fixed & (1u << BUSTER_X86_COST_ADDRESS))
    {
        x86_zen5_fixup(&builder, builder.count, BUSTER_X86_COST_STUB);
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = BUSTER_X86_COST_ADDRESS, .mode = BUSTER_X86_MEMORY_RIP });
    }
    // Every vector register the fixtures name, as wide as the host goes; a legacy SSE case then clears the upper state.
    const uint16_t move = host->avx512 ? X86_FORM_VMOVDQU64_Z_ZM512 : host->avx ? X86_FORM_VMOVDQU_Y_YM256 : X86_FORM_MOVDQU_X_XM;
    for (uint8_t v = 0; v < 16; v += 1)
    {
        const int32_t pattern = v >= BUSTER_X86_COST_VECTOR_IS4 ? BUSTER_X86_COST_CONSTANT_SOURCE : BUSTER_X86_COST_CONSTANT_INITIAL;
        x86_zen5_push(&builder, (X86Instruction){ .form = move, .reg = v, .rm = BUSTER_X86_COST_DATA, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = BUSTER_X86_COST_DATA_CONSTANTS + pattern });
    }
    if (host->avx && shape->description->family == BUSTER_X86_FAMILY_LEGACY && reference == X86_COST_REFERENCE_NONE)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_VZEROUPPER });
    }
    for (uint8_t k = 1; host->avx512 && k < 8; k += 1)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_KMOVQ_K_M64, .reg = k, .rm = BUSTER_X86_COST_DATA, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = BUSTER_X86_COST_DATA_CONSTANTS + BUSTER_X86_COST_CONSTANT_OPMASKS });
    }
    const bool x87 = shape->special == X86_COST_X87 && reference == X86_COST_REFERENCE_NONE;
    if (x87)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_FNINIT });
        x86_zen5_push(&builder, (X86Instruction){ .form = shape->additive ? X86_FORM_FLDZ : X86_FORM_FLD1 });
        for (uint32_t i = 0; i < 5; i += 1)
        {
            x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_FLD1 });
        }
    }
    uint8_t prefix[4096];
    const int64_t prefix_length = builder.valid ? x86_encode_scalar(builder.records, builder.count, prefix, sizeof(prefix), 0, 0) : -1;
    builder.valid &= prefix_length > 0;
    for (int64_t i = 0; builder.valid && i < (64 - prefix_length % 64) % 64; i += 1)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_NOP });
    }
    const uint32_t loop = builder.count;
    builder.helpers = 0;
    if (builder.valid)
    {
        x86_zen5_body(shape, experiment, reference, &builder);
    }
    program->body_count = builder.count - loop;
    program->helper_count = builder.helpers;
    const bool calls = reference == X86_COST_REFERENCE_NONE && shape->special == X86_COST_CONTROL &&
                       (shape->effects->operation == X86_OPERATION_CALL || shape->effects->operation == X86_OPERATION_RET);
    program->extra_count = calls ? BUSTER_X86_COST_BLOCK : 0;
    x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_DEC_RM64, .rm = BUSTER_X86_COST_COUNTER });
    const uint32_t branch = builder.count;
    x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_JNE_REL32 });
    if (x87)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_FNINIT });
    }
    if (host->avx)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_VZEROUPPER });
    }
    for (uint32_t i = BUSTER_ARRAY_LENGTH(saved); i > 0; i -= 1)
    {
        x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_POP_R64, .rm = saved[i - 1] });
    }
    x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_RET });
    const uint32_t stub = builder.count;
    x86_zen5_push(&builder, (X86Instruction){ .form = X86_FORM_RET });
    builder.valid &= x86_validate(builder.records, builder.count) == builder.count;
    if (builder.valid)
    {
        program->code = mmap(0, BUSTER_X86_COST_CODE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        builder.valid = program->code != MAP_FAILED;
        program->code = builder.valid ? program->code : 0;
    }
    if (builder.valid)
    {
        int64_t length = x86_encode_scalar(builder.records, builder.count, program->code, BUSTER_X86_COST_CODE_SIZE, offsets, 0);
        builder.valid = length > 0 && offsets[loop] % 64 == 0;
        offsets[builder.count] = (uint32_t)length;
        // The relative displacements: the loop's branch, then the units' (each to its target record's offset, from the
        // end of the record that names it).
        if (builder.valid)
        {
            builder.records[branch].displacement = (int32_t)offsets[loop] - (int32_t)offsets[branch + 1];
            for (uint32_t f = 0; f < builder.fixups; f += 1)
            {
                const uint32_t record = builder.fixup_records[f];
                const uint32_t target = builder.fixup_targets[f] == BUSTER_X86_COST_STUB ? stub : builder.fixup_targets[f];
                builder.records[record].displacement = (int32_t)offsets[target] - (int32_t)offsets[record + 1];
            }
            const int64_t final = x86_encode_scalar(builder.records, builder.count, program->code, BUSTER_X86_COST_CODE_SIZE, offsets, 0);
            const int64_t vector = x86_encode(builder.records, builder.count, vector_bytes, sizeof(vector_bytes), 0, 0);
            builder.valid = final == length && vector == length && memcmp(vector_bytes, program->code, (size_t)length) == 0;
        }
        if (builder.valid)
        {
            program->length = (uint32_t)length;
            program->hash = UINT64_C(14695981039346656037);
            for (uint32_t i = 0; i < program->length; i += 1)
            {
                program->hash = (program->hash ^ program->code[i]) * UINT64_C(1099511628211);
            }
            builder.valid = mprotect(program->code, BUSTER_X86_COST_CODE_SIZE, PROT_READ | PROT_EXEC) == 0;
        }
    }
    // Whether every record's form is one the host runs: nothing executes the program otherwise.
    program->runnable = builder.valid;
    for (uint32_t r = 0; builder.valid && r < builder.count; r += 1)
    {
        program->runnable &= x86_target_form_allowed(host->allowed, builder.records[r].form);
    }
    if (!builder.valid && program->code != 0)
    {
        munmap(program->code, BUSTER_X86_COST_CODE_SIZE);
        program->code = 0;
    }
    return builder.valid;
}

// Whether every target record of case `shape`'s experiment resolves to the case: its form and operand kind (the
// fixtures never measure a neighbouring case by accident: an index shape, a forced displacement, a zero idiom).
static bool x86_zen5_program_targets(const X86CostShape* shape, X86CostExperiment experiment)
{
    static X86CostBuilder builder;
    builder.count = 0;
    builder.fixups = 0;
    builder.helpers = 0;
    builder.valid = true;
    x86_zen5_body(shape, experiment, X86_COST_REFERENCE_NONE, &builder);
    uint32_t targets = 0;
    for (uint32_t r = 0; builder.valid && r < builder.count; r += 1)
    {
        const X86Instruction* record = &builder.records[r];
        if (builder.kinds[r] == BUSTER_X86_COST_KIND_TARGET)
        {
            builder.valid = record->form == shape->form && x86_zen5_operands(record, shape->form) == shape->operands;
            targets += 1;
        }
    }
    // A unit names its target once; ret's units are calls of the stub whose ret they measure.
    const bool stub = shape->special == X86_COST_CONTROL && shape->effects->operation == X86_OPERATION_RET;
    return builder.valid && targets == (stub ? 0 : BUSTER_X86_COST_BLOCK);
}

// JIT execution is an external ABI boundary, not compiler-path dispatch. Positive iterations are checked at the CLI.
// Generated code has no Clang function-type metadata before its entry point. Disable only that metadata probe;
// ASan and the remaining UBSan checks still cover this wrapper and all construction, model and lookup code.
static __attribute__((no_sanitize("function"))) void x86_zen5_execute_data(const X86CostProgram* program, uint64_t iterations, void* data)
{
    ((void (*)(uint64_t, void*))program->code)(iterations, data);
}

// The data every program of this process addresses (x86_zen5_fill sets it for each case).
static uint8_t x86_zen5_data[BUSTER_X86_COST_DATA_SIZE] __attribute__((aligned(64)));

static void x86_zen5_execute(const X86CostProgram* program, uint64_t iterations)
{
    x86_zen5_execute_data(program, iterations, x86_zen5_data);
}

static void x86_zen5_store(uint8_t* data, uint32_t offset, uint64_t value, uint32_t bytes)
{
    memcpy(data + offset, &value, bytes);
}

// The data of a case: its memory slots (the targets' and the blockers') hold the element its operation reads as a
// constant source, its registers' patterns the element each chain starts from and the constant sources' element.
static void x86_zen5_fill(const X86CostShape* shape)
{
    uint8_t* data = x86_zen5_data;
    const uint32_t width = shape->effects->width >= 1 && shape->effects->width <= 8 ? shape->effects->width : 8;
    uint64_t initial = 3;
    uint32_t size = 4;
    if (shape->fill == X86_COST_FILL_FLOAT)
    {
        initial = 0x3f800000;
    }
    else if (shape->fill == X86_COST_FILL_DOUBLE)
    {
        initial = UINT64_C(0x3ff0000000000000);
        size = 8;
    }
    // A gather's (#1840) index of quadwords (vpgatherqd and vpgatherqq: its VSIB register's bytes over its destination's
    // lanes) starts at quadwords of 3; one of doublewords into quadwords' results reads 0s, so that the chain's next index,
    // its result, stays a valid offset whatever the lanes' sizes.
    const X86FormDescription* description = shape->description;
    const uint32_t index_class = description->rm & BUSTER_X86_OPERAND_CLASS;
    const uint32_t data_class = description->reg & BUSTER_X86_OPERAND_CLASS;
    const uint32_t index_bytes = index_class == BUSTER_X86_CLASS_ZMM ? 64 : index_class == BUSTER_X86_CLASS_YMM ? 32 : 16;
    const uint32_t data_bytes = data_class == BUSTER_X86_CLASS_ZMM ? 64 : data_class == BUSTER_X86_CLASS_YMM ? 32 : 16;
    const bool quadword_index = shape->special == X86_COST_GATHER && index_bytes * width == data_bytes * 8;
    const bool narrow_data = quadword_index && width == 4;
    size = quadword_index ? 8 : size;
    const uint64_t source = shape->additive ? 0 : initial;
    for (uint32_t offset = 0; offset < BUSTER_X86_COST_DATA_CONSTANTS; offset += 8)
    {
        // The slots: the constant source at the operation's width (an integer's 3 at its width too).
        if (narrow_data)
        {
            x86_zen5_store(data, offset, 0, 8);
        }
        else if (shape->fill == X86_COST_FILL_INTEGER)
        {
            for (uint32_t part = 0; part < 8; part += width)
            {
                x86_zen5_store(data, offset + part, 3, width);
            }
        }
        else
        {
            for (uint32_t part = 0; part < 8; part += size)
            {
                x86_zen5_store(data, offset + part, source, size);
            }
        }
    }
    // x87 memory operands: each slot's first bytes are the operand of the form's size.
    const uint32_t stride = x86_zen5_stride(shape);
    for (uint32_t offset = 0; offset < BUSTER_X86_COST_DATA_TARGETS; offset += stride)
    {
        switch (shape->form)
        {
            case X86_FORM_FLD_M80:
                x86_zen5_store(data, offset, UINT64_C(0x8000000000000000), 8);
                x86_zen5_store(data, offset + 8, 0x3fff, 2);
                break;
            case X86_FORM_FLD_M32:
            case X86_FORM_FADD_M32:
                x86_zen5_store(data, offset, shape->form == X86_FORM_FADD_M32 ? 0 : 0x3f800000, 4);
                break;
            case X86_FORM_FLD_M64:
                x86_zen5_store(data, offset, UINT64_C(0x3ff0000000000000), 8);
                break;
            case X86_FORM_FLDCW_M16:
                x86_zen5_store(data, offset, 0x037f, 2);
                break;
            default:
                break;
        }
    }
    const uint32_t constants = BUSTER_X86_COST_DATA_CONSTANTS;
    for (uint32_t part = 0; part < 64; part += size)
    {
        x86_zen5_store(data, constants + BUSTER_X86_COST_CONSTANT_INITIAL + part, initial, size);
        x86_zen5_store(data, constants + BUSTER_X86_COST_CONSTANT_SOURCE + part, shape->fill == X86_COST_FILL_INTEGER ? initial : source, size);
    }
    // The dividend: 57 bits, positive at both widths' signed reading with RDX zero.
    x86_zen5_store(data, constants + BUSTER_X86_COST_CONSTANT_DIVIDEND, UINT64_C(0x0123456789abcdef), 8);
    x86_zen5_store(data, constants + BUSTER_X86_COST_CONSTANT_OPMASKS, UINT64_C(0x5555555555555555), 8);
}

typedef struct X86CostCounters X86CostCounters;
struct X86CostCounters
{
    int fd[BUSTER_X86_COST_COUNTERS];
};

// Core cycles, retired instructions, then AMD's raw events: PMCx0C1 retired macro-ops and PMCx0AB's macro-ops dispatched
// to the floating-point unit (umask 4) and to the integer unit (umask 8), the host's `perf list --details`'s
// ex_ret_ops and de_dis_ops_from_decoder.any_fp_dispatch and .any_integer_dispatch.
typedef struct X86CostSample X86CostSample;
struct X86CostSample
{
    uint64_t count;
    uint64_t enabled;
    uint64_t running;
    uint64_t values[BUSTER_X86_COST_COUNTERS];
};

static bool x86_zen5_counters(X86CostCounters* counters)
{
    static const uint64_t raw[BUSTER_X86_COST_COUNTERS] = { 0, 0, 0xc1, 0x04ab, 0x08ab };
    bool valid = true;
    for (uint32_t i = 0; i < BUSTER_X86_COST_COUNTERS; i += 1)
    {
        counters->fd[i] = -1;
        if (valid)
        {
            struct perf_event_attr attributes = {
                .size = sizeof(attributes),
                .type = i >= 2 ? PERF_TYPE_RAW : PERF_TYPE_HARDWARE,
                .config = i == 0 ? PERF_COUNT_HW_CPU_CYCLES : i == 1 ? PERF_COUNT_HW_INSTRUCTIONS : raw[i],
                .disabled = i == 0,
                .pinned = i == 0,
                .exclude_kernel = 1,
                .exclude_hv = 1,
                .read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING,
            };
            counters->fd[i] = (int)syscall(SYS_perf_event_open, &attributes, 0, -1, i == 0 ? -1 : counters->fd[0], PERF_FLAG_FD_CLOEXEC);
            valid = counters->fd[i] >= 0;
        }
    }
    return valid;
}

static void x86_zen5_counters_close(X86CostCounters* counters)
{
    for (uint32_t i = 0; i < BUSTER_X86_COST_COUNTERS; i += 1)
    {
        if (counters->fd[i] >= 0)
        {
            close(counters->fd[i]);
            counters->fd[i] = -1;
        }
    }
}

static bool x86_zen5_sample(const X86CostProgram* program, uint64_t iterations, const X86CostCounters* counters, X86CostSample* sample)
{
    bool valid = ioctl(counters->fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == 0;
    valid &= ioctl(counters->fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == 0;
    if (valid)
    {
        x86_zen5_execute(program, iterations);
    }
    valid &= ioctl(counters->fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0;
    valid &= read(counters->fd[0], sample, sizeof(*sample)) == sizeof(*sample);
    valid &= sample->count == BUSTER_X86_COST_COUNTERS && sample->running != 0 && sample->enabled == sample->running;
    const uint64_t expected = iterations * (program->body_count + program->extra_count + 2);
    // Besides the loop: initialization, saves, restores, call and counter-control wrappers. A broad fixed bound
    // detects a wrong branch/count or a missing event without converting arbitrary counts into apparent evidence.
    valid &= sample->values[1] >= expected && sample->values[1] - expected < 4096;
    valid &= sample->values[0] != 0 && sample->values[2] != 0;
    if (!valid)
    {
        fprintf(stderr, "invalid counters: count=%lu enabled=%lu running=%lu cycles=%lu instructions=%lu expected=%lu macro-ops=%lu\n",
            sample->count, sample->enabled, sample->running, sample->values[0], sample->values[1], expected, sample->values[2]);
    }
    return valid;
}

static uint64_t x86_zen5_median(uint64_t* values, uint32_t count)
{
    for (uint32_t i = 1; i < count; i += 1)
    {
        const uint64_t value = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            j -= 1;
        }
        values[j] = value;
    }
    return values[count / 2];
}

static bool x86_zen5_number(const char* text, uint64_t minimum, uint64_t maximum, uint64_t* output)
{
    uint64_t value = 0;
    bool valid = text[0] != 0;
    for (uint64_t i = 0; valid && text[i] != 0; i += 1)
    {
        const uint32_t digit = (uint8_t)text[i] - '0';
        valid = digit <= 9 && value <= (maximum - digit) / 10;
        if (valid)
        {
            value = value * 10 + digit;
        }
    }
    valid &= value >= minimum && value <= maximum;
    if (valid)
    {
        *output = value;
    }
    return valid;
}

// One experiment's medians per unit: cycles (and the passes' least and greatest), retired macro-ops, FP and integer
// dispatched macro-ops and retired instructions, the latter four with the empty loop's subtracted.
typedef struct X86CostObservation X86CostObservation;
struct X86CostObservation
{
    double cycles;
    double minimum;
    double maximum;
    double ops;
    double fp;
    double integer;
    double instructions;
    // Helper records per unit.
    double helpers;
    uint64_t hash;
    bool ran;
};

// Measures `count` programs in `passes` interleaved passes (each pass starting with the empty loop and rotating the
// programs' order), into `observations`.
static bool x86_zen5_observe(const X86CostProgram* programs, uint32_t count, const X86CostProgram* empty, uint64_t iterations, uint32_t passes, const X86CostCounters* counters,
                             X86CostObservation* observations)
{
    static uint64_t values[X86_COST_EXPERIMENT_COUNT][BUSTER_X86_COST_COUNTERS][BUSTER_X86_COST_MAX_PASSES];
    bool valid = count <= X86_COST_EXPERIMENT_COUNT;
    for (uint32_t p = 0; valid && p < count; p += 1)
    {
        if (programs[p].code != 0)
        {
            x86_zen5_execute(&programs[p], iterations);
        }
    }
    for (uint32_t pass = 0; valid && pass < passes; pass += 1)
    {
        X86CostSample baseline = { 0 };
        valid = x86_zen5_sample(empty, iterations, counters, &baseline);
        for (uint32_t j = 0; valid && j < count; j += 1)
        {
            const uint32_t p = (pass + j) % count;
            if (programs[p].code != 0)
            {
                X86CostSample sample = { 0 };
                valid = x86_zen5_sample(&programs[p], iterations, counters, &sample);
                for (uint32_t c = 0; valid && c < BUSTER_X86_COST_COUNTERS; c += 1)
                {
                    // Cycles keep the loop's overhead; the counts subtract the empty loop's.
                    const uint64_t subtract = c == 0 ? 0 : baseline.values[c];
                    values[p][c][pass] = sample.values[c] >= subtract ? sample.values[c] - subtract : 0;
                }
            }
        }
    }
    const double units = (double)iterations * BUSTER_X86_COST_BLOCK;
    for (uint32_t p = 0; valid && p < count; p += 1)
    {
        X86CostObservation* observation = &observations[p];
        memset(observation, 0, sizeof(*observation));
        if (programs[p].code != 0)
        {
            double extremes[2] = { 1e300, 0 };
            for (uint32_t pass = 0; pass < passes; pass += 1)
            {
                const double cycles = (double)values[p][0][pass] / units;
                extremes[0] = cycles < extremes[0] ? cycles : extremes[0];
                extremes[1] = cycles > extremes[1] ? cycles : extremes[1];
            }
            observation->cycles = (double)x86_zen5_median(values[p][0], passes) / units;
            observation->instructions = (double)x86_zen5_median(values[p][1], passes) / units;
            observation->ops = (double)x86_zen5_median(values[p][2], passes) / units;
            observation->fp = (double)x86_zen5_median(values[p][3], passes) / units;
            observation->integer = (double)x86_zen5_median(values[p][4], passes) / units;
            observation->minimum = extremes[0];
            observation->maximum = extremes[1];
            observation->helpers = (double)programs[p].helper_count / BUSTER_X86_COST_BLOCK;
            observation->hash = programs[p].hash;
            observation->ran = true;
        }
    }
    return valid;
}

// A row's values, fixed point (BUSTER_X86_COST_SCALE units), and how each was obtained.
typedef struct X86CostRow X86CostRow;
struct X86CostRow
{
    uint16_t values[X86_COST_COLUMN_COUNT];
    uint8_t measured;
    uint8_t derived;
    uint8_t absent;
};

// Cycles to fixed point: a latency within a tenth of a whole cycle is that cycle, otherwise an eighth; a throughput is
// a 64th of a cycle (at least one); an op count an eighth. The coarse steps let equal costs share an ID.
static uint16_t x86_zen5_fixed(double value, uint32_t column)
{
    const double clamped = value < 0 ? 0 : value > 255 ? 255 : value;
    const double whole = __builtin_round(clamped);
    double step = 1.0 / 8;
    if (column == X86_COST_THROUGHPUT)
    {
        step = 1.0 / 64;
    }
    else if (column != X86_COST_RETIRED_OPS && column != X86_COST_FP_OPS && __builtin_fabs(clamped - whole) <= 0.1)
    {
        step = 1;
    }
    double rounded = __builtin_round(clamped / step) * step;
    rounded = column == X86_COST_THROUGHPUT && rounded < step ? step : rounded;
    return (uint16_t)__builtin_round(rounded * BUSTER_X86_COST_SCALE);
}

static void x86_zen5_set(X86CostRow* row, uint32_t column, double value, bool derived)
{
    row->values[column] = x86_zen5_fixed(value, column);
    row->measured |= derived ? 0 : 1u << column;
    row->derived |= derived ? 1u << column : 0;
}

// Whether a form's memory kind is a load (or a move) with no operation of its register kind: a move's, an extension's,
// a vector move's, a transfer's, a broadcast's, an x87 load's. Its load's extra latency is its whole address path.
static bool x86_zen5_moves(uint32_t operation)
{
    return operation == X86_OPERATION_MOV || operation == X86_OPERATION_MOVZX || operation == X86_OPERATION_MOVSX || operation == X86_OPERATION_FMOVE ||
           operation == X86_OPERATION_VECTOR_MOVE || operation == X86_OPERATION_MOVD || operation == X86_OPERATION_X87_LOAD ||
           operation == X86_OPERATION_VECTOR_SHUFFLE;
}

// The row of a case from its observations (docs/x86-zen5.md, "Reduction"): throughput, ops and dispatch from the
// twelve independent units (less their helpers, one macro-op each, derived); the latency from the longest register
// chain or the address chain (less their bridges' shares, derived); the load's extra latency as the address chain's
// excess over the register paths (or the flags', for a flags result; a memory kind without either, over its form's
// register kind's, `register_row`, unless it only loads); the flags' latency from their chain (less the cmov's and a
// bridge's share); and a pipe bit for each blocker whose mix took at least half the cycles that full sharing would add
// over the mix's lower bound.
static X86CostRow x86_zen5_reduce(const X86CostShape* shape, const X86CostObservation* observations, const X86CostReferences* references, const X86CostRow* register_row)
{
    X86CostRow row = { .values = { BUSTER_X86_COST_UNKNOWN, BUSTER_X86_COST_UNKNOWN, BUSTER_X86_COST_UNKNOWN, BUSTER_X86_COST_UNKNOWN, BUSTER_X86_COST_UNKNOWN,
                                   BUSTER_X86_COST_UNKNOWN, BUSTER_X86_COST_UNKNOWN } };
    const X86CostObservation* independent = &observations[X86_COST_INDEPENDENT];
    const uint32_t destination = shape->destination;
    const uint32_t result_file = destination != BUSTER_X86_COST_NO_ROLE ? shape->files[destination] : X86_COST_FILE_NONE;
    if (independent->ran)
    {
        const bool helped = independent->helpers != 0;
        // x87 helpers (fld, fstp, fxch) and a gather's kxnorw dispatch to the FP unit; the others to the integer one.
        const bool fp_helpers = shape->special == X86_COST_X87 || shape->special == X86_COST_GATHER;
        x86_zen5_set(&row, X86_COST_THROUGHPUT, independent->cycles, helped);
        x86_zen5_set(&row, X86_COST_RETIRED_OPS, independent->ops - independent->helpers, helped);
        x86_zen5_set(&row, X86_COST_FP_OPS, independent->fp - (fp_helpers ? independent->helpers : 0), helped && fp_helpers);
    }
    // The register paths.
    double latency = -1;
    bool latency_derived = false;
    for (uint32_t e = X86_COST_CHAIN_DESTINATION; e <= X86_COST_CHAIN_INDEX; e += 1)
    {
        const X86CostObservation* chain = &observations[e];
        if (chain->ran)
        {
            double value = chain->cycles;
            bool derived = false;
            if (e >= X86_COST_CHAIN_REG && e <= X86_COST_CHAIN_VVVV && !shape->lea)
            {
                const double share = x86_zen5_bridge_share(references, result_file, shape->files[e - X86_COST_CHAIN_REG]);
                value -= share;
                derived = share != 0;
            }
            else if (e == X86_COST_CHAIN_DESTINATION && shape->special == X86_COST_IMPLICIT &&
                     (shape->effects->operation == X86_OPERATION_DIV || shape->effects->operation == X86_OPERATION_IDIV || shape->effects->operation == X86_OPERATION_SIGN_SPLIT))
            {
                // The or that restores the dividend (or RDX's sign into RAX): one ALU cycle.
                value -= 1;
                derived = true;
            }
            if (value > latency)
            {
                latency = value;
                latency_derived = derived;
            }
        }
    }
    double flags = -1;
    const X86CostObservation* flags_chain = &observations[X86_COST_CHAIN_FLAGS];
    if (flags_chain->ran)
    {
        uint32_t source_file = X86_COST_FILE_GPR;
        for (uint32_t role = BUSTER_X86_COST_ROLES; role > 0; role -= 1)
        {
            const uint32_t file = shape->files[role - 1];
            source_file = (shape->access[role - 1] & BUSTER_X86_ROLE_READ) && (file == X86_COST_FILE_GPR || file == X86_COST_FILE_VECTOR) ? file : source_file;
        }
        flags = flags_chain->cycles - references->flags / 2 - x86_zen5_bridge_share(references, X86_COST_FILE_GPR, source_file);
        x86_zen5_set(&row, X86_COST_FLAGS_LATENCY, flags, true);
    }
    const X86CostObservation* address_chain = &observations[X86_COST_CHAIN_ADDRESS];
    if (address_chain->ran)
    {
        // Less the and and the add into the base (an ALU cycle each), and the result's way into a general register.
        const double share = result_file == X86_COST_FILE_NONE ? references->flags / 2 : x86_zen5_bridge_share(references, result_file, X86_COST_FILE_GPR);
        const double address = address_chain->cycles - 2 - share;
        double base = result_file != X86_COST_FILE_NONE ? (latency > 0 ? latency : 0) : (flags > 0 ? flags : 0);
        if (base == 0 && register_row != 0 && !x86_zen5_moves(shape->effects->operation))
        {
            const uint32_t column = result_file != X86_COST_FILE_NONE ? X86_COST_LATENCY : X86_COST_FLAGS_LATENCY;
            const uint16_t value = register_row->values[column];
            base = value != BUSTER_X86_COST_UNKNOWN ? (double)value / BUSTER_X86_COST_SCALE : 0;
        }
        x86_zen5_set(&row, X86_COST_LOAD_EXTRA, address - base, true);
        if (result_file != X86_COST_FILE_NONE && address > latency)
        {
            latency = address;
            latency_derived = true;
        }
    }
    if (latency >= 0 && result_file != X86_COST_FILE_NONE)
    {
        x86_zen5_set(&row, X86_COST_LATENCY, latency, latency_derived);
    }
    else if (latency >= 0 && shape->stores && shape->loads)
    {
        // A read-modify-write's chain through its memory: the load, the operation and the store's forwarding.
        x86_zen5_set(&row, X86_COST_LATENCY, latency, latency_derived);
    }
    else if (latency >= 0 && (shape->special == X86_COST_X87 || shape->special == X86_COST_IMPLICIT))
    {
        // An x87 stack register's chain, or an implicit result register's (RAX, or RDX through RAX).
        x86_zen5_set(&row, X86_COST_LATENCY, latency, latency_derived);
    }
    else if (shape->special == X86_COST_GATHER && observations[X86_COST_CHAIN_INDEX].ran)
    {
        x86_zen5_set(&row, X86_COST_LATENCY, observations[X86_COST_CHAIN_INDEX].cycles, false);
    }
    // The pipes.
    const X86CostObservation* six = &observations[X86_COST_INDEPENDENT_SPARE];
    uint32_t pipes = 0;
    bool mixed = false;
    for (uint32_t b = 0; six->ran && b < BUSTER_X86_PIPE_BLOCKERS; b += 1)
    {
        const X86CostObservation* mix = &observations[X86_COST_MIX_FIRST + b];
        if (mix->ran)
        {
            const double blocker = references->blocker_cycles[b];
            const double dispatch = (six->ops + references->blocker_ops[b]) / 8;
            double floor = six->cycles > blocker ? six->cycles : blocker;
            floor = dispatch > floor ? dispatch : floor;
            const double shared = six->cycles + blocker;
            pipes |= shared - floor >= 0.05 && mix->cycles - floor >= 0.5 * (shared - floor) ? 1u << b : 0;
            mixed = true;
        }
    }
    if (mixed)
    {
        row.values[X86_COST_PIPES] = (uint16_t)pipes;
        row.measured |= 1u << X86_COST_PIPES;
    }
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        row.absent |= ((row.measured | row.derived) & (1u << column)) == 0 ? 1u << column : 0;
    }
    return row;
}

static void x86_zen5_print_value(uint16_t value)
{
    if (value == BUSTER_X86_COST_UNKNOWN)
    {
        printf("U");
    }
    else
    {
        printf("%u", value);
    }
}

// The row as x86_zen5_rows.c has it.
static void x86_zen5_print_row(const char* name, const X86CostRow* row)
{
    const char* slash = strchr(name, '/');
    const int form_length = slash != 0 ? (int)(slash - name) : (int)strlen(name);
    printf("    X(%.*s, %s", form_length, name, slash != 0 ? slash + 1 : "");
    for (uint32_t column = 0; column < X86_COST_COLUMN_COUNT; column += 1)
    {
        printf(", ");
        x86_zen5_print_value(row->values[column]);
    }
    printf(", 0x%02x, 0x%02x, 0x%02x) \\\n", row->measured, row->derived, row->absent);
}

static void x86_zen5_print_observation(const char* name, const char* experiment, const X86CostObservation* observation)
{
    printf("%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%016lx\n", name, experiment, observation->cycles, observation->minimum, observation->maximum, observation->ops,
           observation->fp, observation->integer, observation->instructions, observation->hash);
}

// Whether `measured` reproduces the table's row `entry`: the same masks, each latency within half a cycle or 10%, each
// throughput within a 32nd of a cycle or 10% of this run's median or inside its passes' range (`independent`'s least
// and greatest: kortest's and ktest's passes are 0.5 or 1.0 cycles, either way in one process), each op count within an
// eighth or 5%. The pipes are reported, not required: a bit near its threshold can change between processes
// (docs/x86-zen5.md).
static bool x86_zen5_reproduces(const X86CostCase* entry, const X86CostRow* measured, const X86CostObservation* independent)
{
    bool same = entry->measured == measured->measured && entry->derived == measured->derived && entry->absent == measured->absent;
    for (uint32_t column = 0; same && column < X86_COST_PIPES; column += 1)
    {
        const double table = entry->values[column];
        const double value = measured->values[column];
        const double difference = __builtin_fabs(table - value);
        double tolerance = 0;
        switch (column)
        {
            case X86_COST_THROUGHPUT:
                tolerance = 8 > 0.1 * table ? 8 : 0.1 * table;
                break;
            case X86_COST_RETIRED_OPS:
            case X86_COST_FP_OPS:
                tolerance = 32 + 0.05 * table;
                break;
            default:
                tolerance = 128 > 0.1 * table ? 128 : 0.1 * table;
                break;
        }
        bool within = table == value || (table != BUSTER_X86_COST_UNKNOWN && value != BUSTER_X86_COST_UNKNOWN && difference <= tolerance);
        if (column == X86_COST_THROUGHPUT && !within && table != BUSTER_X86_COST_UNKNOWN && independent->ran)
        {
            const double cycles = table / BUSTER_X86_COST_SCALE;
            within = cycles >= independent->minimum * 0.9 - 1.0 / 32 && cycles <= independent->maximum * 1.1 + 1.0 / 32;
        }
        same = within;
    }
    return same;
}

static bool x86_zen5_approved(char* brand, uint32_t* signature)
{
    uint32_t eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    *signature = eax;
    memset(brand, 0, 49);
    for (uint32_t i = 0; i < 3; i += 1)
    {
        uint32_t words[4];
        __cpuid(0x80000002 + i, words[0], words[1], words[2], words[3]);
        memcpy(brand + 16 * i, words, 16);
    }
    return *signature == 0xb40f40 && strstr(brand, "AMD Ryzen 7 9700X") != 0;
}

// The references: the empty loop, the bridges' round trips and each blocker alone.
static bool x86_zen5_references(const X86CostHost* host, uint64_t iterations, uint32_t passes, const X86CostCounters* counters, const X86CostProgram* empty,
                                X86CostReferences* references)
{
    static const char* const names[X86_COST_REFERENCE_COUNT] = { "", "", "vector-round-trip", "opmask-round-trip", "flags-round-trip" };
    X86CostShape shape;
    x86_zen5_shape(X86_FORM_NOP, X86_COST_REGISTER, &shape);
    X86CostProgram programs[X86_COST_REFERENCE_COUNT] = { 0 };
    X86CostObservation observations[X86_COST_REFERENCE_COUNT];
    bool valid = true;
    for (uint32_t r = X86_COST_REFERENCE_VECTOR; valid && r < X86_COST_REFERENCE_COUNT; r += 1)
    {
        valid = x86_zen5_program(&shape, X86_COST_INDEPENDENT, r, host, &programs[r]) && programs[r].runnable;
    }
    valid = valid && x86_zen5_observe(programs + X86_COST_REFERENCE_VECTOR, X86_COST_REFERENCE_COUNT - X86_COST_REFERENCE_VECTOR, empty, iterations, passes, counters,
                                      observations + X86_COST_REFERENCE_VECTOR);
    for (uint32_t r = X86_COST_REFERENCE_VECTOR; valid && r < X86_COST_REFERENCE_COUNT; r += 1)
    {
        const char* name = r >= X86_COST_REFERENCE_BLOCKER ? x86_zen5_experiment_names[X86_COST_MIX_FIRST + r - X86_COST_REFERENCE_BLOCKER] : names[r];
        x86_zen5_print_observation("reference", name, &observations[r]);
    }
    if (valid)
    {
        references->vector = observations[X86_COST_REFERENCE_VECTOR].cycles;
        references->opmask = observations[X86_COST_REFERENCE_OPMASK].cycles;
        references->flags = observations[X86_COST_REFERENCE_FLAGS].cycles;
        for (uint32_t b = 0; b < BUSTER_X86_PIPE_BLOCKERS; b += 1)
        {
            references->blocker_cycles[b] = observations[X86_COST_REFERENCE_BLOCKER + b].cycles;
            references->blocker_ops[b] = observations[X86_COST_REFERENCE_BLOCKER + b].ops;
        }
    }
    for (uint32_t r = 0; r < X86_COST_REFERENCE_COUNT; r += 1)
    {
        if (programs[r].code != 0)
        {
            munmap(programs[r].code, BUSTER_X86_COST_CODE_SIZE);
        }
    }
    return valid;
}

// Builds case `c`'s programs: every experiment its shape allows, each target checked against the case.
static bool x86_zen5_case_programs(uint32_t c, const X86CostHost* host, X86CostShape* shape, X86CostProgram* programs)
{
    const X86CostCase* entry = &x86_zen5_cases[c];
    x86_zen5_shape(entry->form, entry->operands, shape);
    bool valid = shape->special == X86_COST_TRAP || shape->experiments != 0;
    if (!valid)
    {
        fprintf(stderr, "%s: no fixture (a form outside the effects tables)\n", entry->name);
    }
    for (uint32_t e = 0; valid && e < X86_COST_EXPERIMENT_COUNT; e += 1)
    {
        if (shape->experiments & (1u << e))
        {
            valid = x86_zen5_program_targets(shape, (X86CostExperiment)e) && x86_zen5_program(shape, (X86CostExperiment)e, X86_COST_REFERENCE_NONE, host, &programs[e]) &&
                    programs[e].runnable;
            if (!valid)
            {
                fprintf(stderr, "%s: the %s fixture can't be built\n", entry->name, x86_zen5_experiment_names[e]);
            }
        }
    }
    return valid;
}

static void x86_zen5_release(X86CostProgram* programs, uint32_t count)
{
    for (uint32_t p = 0; p < count; p += 1)
    {
        if (programs[p].code != 0)
        {
            munmap(programs[p].code, BUSTER_X86_COST_CODE_SIZE);
            programs[p].code = 0;
        }
    }
}

// measure-x86-zen5 [--iterations N] [--passes N] [--only FORM/KIND | --sample N [--offset N] | --missing] [--table]
// [--check] [--immediate N] [--decorations N]
static int run_x86_zen5_measure(int argc, char** argv)
{
    uint64_t iterations = 10000;
    uint64_t passes = 7;
    uint64_t sample = 1;
    uint64_t offset = 0;
    const char* only = 0;
    bool table = false;
    bool check = false;
    bool missing = false;
    bool valid = true;
    for (int i = 0; i < argc && valid; i += 1)
    {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 1, BUSTER_X86_COST_MAX_ITERATIONS, &iterations);
        }
        else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 3, BUSTER_X86_COST_MAX_PASSES, &passes) && (passes & 1) != 0;
        }
        else if (strcmp(argv[i], "--sample") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 1, X86_COST_CASE_COUNT, &sample);
        }
        else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc)
        {
            valid = x86_zen5_number(argv[++i], 0, X86_COST_CASE_COUNT, &offset);
        }
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
        {
            only = argv[++i];
        }
        else if (strcmp(argv[i], "--immediate") == 0 && i + 1 < argc)
        {
            const char* text = argv[++i];
            uint64_t magnitude = 0;
            valid = x86_zen5_number(text + (text[0] == '-'), 0, UINT32_MAX, &magnitude);
            x86_zen5_override_immediate = text[0] == '-' ? -(int64_t)magnitude : (int64_t)magnitude;
        }
        else if (strcmp(argv[i], "--decorations") == 0 && i + 1 < argc)
        {
            uint64_t decorations = 0;
            valid = x86_zen5_number(argv[++i], 0, 255, &decorations);
            x86_zen5_override_decorations = (int32_t)decorations;
        }
        else if (strcmp(argv[i], "--table") == 0)
        {
            table = true;
        }
        else if (strcmp(argv[i], "--check") == 0)
        {
            check = true;
        }
        else if (strcmp(argv[i], "--missing") == 0)
        {
            missing = true;
        }
        else
        {
            valid = false;
        }
    }
    char brand[49];
    uint32_t signature = 0;
    if (valid && !x86_zen5_approved(brand, &signature))
    {
        fprintf(stderr, "measure-x86-zen5 requires the approved 9700X, got %s (CPUID %08x)\n", brand, signature);
        valid = false;
    }
    static X86CostHost host;
    X86CostCounters counters = { .fd = { -1, -1, -1, -1, -1 } };
    X86CostProgram empty = { 0 };
    X86CostReferences references = { 0 };
    if (valid)
    {
        valid = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize();
        x86_zen5_host(&host);
        X86CostShape shape;
        x86_zen5_shape(X86_FORM_NOP, X86_COST_REGISTER, &shape);
        valid = valid && x86_zen5_counters(&counters) && x86_zen5_program(&shape, X86_COST_INDEPENDENT, X86_COST_REFERENCE_EMPTY, &host, &empty) && empty.runnable;
    }
    if (valid)
    {
        printf("# CPU=%s CPUID=%08x compiler=%s iterations=%lu passes=%lu block=%u\n", brand, signature, __clang_version__, iterations, passes, BUSTER_X86_COST_BLOCK);
        printf("# per unit: core cycles (median, min, max), retired macro-ops, FP- and integer-dispatched macro-ops and retired instructions with the empty loop's\n");
        printf("# subtracted; a mix's unit is a target and its blocker, a chain's a target and its bridges, a helper's records are in its unit\n");
        printf("case\texperiment\tcycles\tmin\tmax\tretired_ops\tfp_ops\tinteger_ops\tinstructions\tcode_hash\n");
        X86CostShape shape;
        x86_zen5_shape(X86_FORM_NOP, X86_COST_REGISTER, &shape);
        x86_zen5_fill(&shape);
        valid = x86_zen5_references(&host, iterations, (uint32_t)passes, &counters, &empty, &references);
    }
    static X86CostRow rows[X86_COST_CASE_COUNT];
    static bool fresh[X86_COST_CASE_COUNT];
    uint32_t matched = 0;
    uint32_t reproduced = 0;
    uint32_t pipe_differences = 0;
    for (uint32_t c = 1; valid && c < X86_COST_CASE_COUNT; c += 1)
    {
        const X86CostCase* entry = &x86_zen5_cases[c];
        // --missing: the rows that account for no column yet, the ones added with U values and zero masks.
        const bool unmeasured = (entry->measured | entry->derived | entry->absent) == 0;
        const bool selected = only != 0 ? strcmp(only, entry->name) == 0 : missing ? unmeasured : (c - 1) % sample == offset % sample;
        if (selected)
        {
            matched += 1;
            X86CostShape shape;
            static X86CostProgram programs[X86_COST_EXPERIMENT_COUNT];
            static X86CostObservation observations[X86_COST_EXPERIMENT_COUNT];
            memset(programs, 0, sizeof(programs));
            memset(observations, 0, sizeof(observations));
            valid = x86_zen5_case_programs(c, &host, &shape, programs);
            if (valid)
            {
                x86_zen5_fill(&shape);
                valid = x86_zen5_observe(programs, X86_COST_EXPERIMENT_COUNT, &empty, iterations, (uint32_t)passes, &counters, observations);
            }
            for (uint32_t e = 0; valid && e < X86_COST_EXPERIMENT_COUNT; e += 1)
            {
                if (observations[e].ran)
                {
                    x86_zen5_print_observation(entry->name, x86_zen5_experiment_names[e], &observations[e]);
                }
            }
            if (valid)
            {
                // The form's register kind's row, this run's or the table's, for a memory kind's load.
                const uint32_t register_case = x86_zen5_case_find(entry->form, X86_COST_REGISTER);
                X86CostRow table_row = { 0 };
                const X86CostRow* register_row = 0;
                if (register_case != X86_COST_CASE_NONE && register_case != c)
                {
                    memcpy(table_row.values, x86_zen5_cases[register_case].values, sizeof(table_row.values));
                    register_row = fresh[register_case] ? &rows[register_case] : &table_row;
                }
                rows[c] = x86_zen5_reduce(&shape, observations, &references, register_row);
                fresh[c] = true;
                printf("# row ");
                x86_zen5_print_row(entry->name, &rows[c]);
                if (check)
                {
                    const bool same = x86_zen5_reproduces(entry, &rows[c], &observations[X86_COST_INDEPENDENT]);
                    reproduced += same;
                    pipe_differences += entry->values[X86_COST_PIPES] != rows[c].values[X86_COST_PIPES];
                    if (!same)
                    {
                        printf("# differs from the table: ");
                        x86_zen5_print_row(entry->name, &(X86CostRow){ .values = { entry->values[0], entry->values[1], entry->values[2], entry->values[3], entry->values[4],
                                                                                  entry->values[5], entry->values[6] },
                                                                       .measured = entry->measured, .derived = entry->derived, .absent = entry->absent });
                    }
                }
            }
            else
            {
                fprintf(stderr, "measurement failed for %s\n", entry->name);
            }
            x86_zen5_release(programs, X86_COST_EXPERIMENT_COUNT);
        }
    }
    valid &= matched != 0;
    if (valid && check)
    {
        printf("# check: %u of %u cases reproduce the table; %u differ in pipes (reported, not required)\n", reproduced, matched, pipe_differences);
        valid = reproduced == matched;
    }
    if (valid && table)
    {
        printf("# table\n");
        for (uint32_t c = 1; c < X86_COST_CASE_COUNT; c += 1)
        {
            const X86CostCase* entry = &x86_zen5_cases[c];
            const X86CostRow kept = { .values = { entry->values[0], entry->values[1], entry->values[2], entry->values[3], entry->values[4], entry->values[5], entry->values[6] },
                                      .measured = entry->measured,
                                      .derived = entry->derived,
                                      .absent = entry->absent };
            x86_zen5_print_row(entry->name, fresh[c] ? &rows[c] : &kept);
        }
    }
    x86_zen5_counters_close(&counters);
    x86_zen5_release(&empty, 1);
    if (!valid)
    {
        fprintf(stderr, "measure-x86-zen5 failed: check options/case name, CPU, perf permissions, pinned counters, generated loop counts and --check's differences; no missing counter is treated as zero\n");
    }
    return valid ? 0 : 1;
}
