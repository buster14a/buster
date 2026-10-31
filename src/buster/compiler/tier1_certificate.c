// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Allocation certificate checker for a borrowed, normalized machine-SSA view.
// The source, target layout, ABI facts, recipes and CFG/phi maps come from the
// verified preallocation program, NOT the allocation certificate. Code records
// are decoded from the rewritten stream; opcode/immediate/effect identify ALL
// non-location semantics. This module is not an x86 byte decoder.
//
// One location per value is certified at a block entry. Equal-width aliases at
// the same location are supported, as are arbitrary copies after entry. Each
// byte carries an identity and lane. Definitions change the value's identity,
// not every old home. A frame recipe binds the base incarnation current when
// its value's incarnation was established, so a later base definition cannot
// retarget a rematerialization. Blocks use epochs; edges use a stamped sparse
// overlay. Scratch is caller-owned and must not overlap any input or the
// report. See docs/tier1-certificate.md for the trust boundary and unsupported
// inputs.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define BUSTER_CERT_NONE UINT32_MAX
#define BUSTER_CERT_MAX_RECORDS (1u << 20)
#define BUSTER_CERT_MAX_TOTAL (1u << 24)
#define BUSTER_CERT_MAX_OPERANDS 8
#define BUSTER_CERT_MAX_WIDTH 64
#define BUSTER_CERT_MEMORY 1u
#define BUSTER_CERT_AVX512F 1u
#define BUSTER_CERT_AVX512DQ 2u
#define BUSTER_CERT_AVX512BW 4u
#define BUSTER_CERT_AVX2 8u

typedef enum Tier1CertClass
{
    TIER1_CERT_GPR,
    TIER1_CERT_VECTOR,
    TIER1_CERT_MASK,
    TIER1_CERT_STACK,
    TIER1_CERT_FLAGS,
} Tier1CertClass;

typedef enum Tier1CertOp
{
    TIER1_CERT_ORIGINAL,
    TIER1_CERT_MOVE,
    TIER1_CERT_XCHG,
    TIER1_CERT_XOR,
    TIER1_CERT_CONSTANT,
    TIER1_CERT_FRAME_ADDRESS,
} Tier1CertOp;

typedef enum Tier1CertRecipe
{
    TIER1_CERT_NO_RECIPE,
    TIER1_CERT_INTEGER_RECIPE,
    TIER1_CERT_FRAME_RECIPE,
} Tier1CertRecipe;

typedef enum Tier1CertError
{
    TIER1_CERT_OK,
    TIER1_CERT_INVALID,
    TIER1_CERT_CAPACITY,
    TIER1_CERT_CORRESPONDENCE,
    TIER1_CERT_VALUE,
    TIER1_CERT_TRANSITION,
    TIER1_CERT_ENTRY,
    TIER1_CERT_EDGE,
} Tier1CertError;

typedef struct Tier1CertLocation Tier1CertLocation;
struct Tier1CertLocation
{
    uint32_t unit;
    uint16_t width;
    uint16_t extent;
    uint8_t class;
    uint8_t reg;
};

typedef struct Tier1CertValue Tier1CertValue;
struct Tier1CertValue
{
    uint64_t payload;
    uint32_t dependency;
    uint16_t width;
    uint8_t class;
    uint8_t recipe;
    bool stable;
};

typedef struct Tier1CertFact Tier1CertFact;
struct Tier1CertFact
{
    uint32_t value;
    uint32_t location;
};

typedef struct Tier1CertOperand Tier1CertOperand;
struct Tier1CertOperand
{
    uint32_t value;
    uint32_t allowed_registers;
    uint8_t flags;
    // One-based use number for a tied definition; zero means no tie.
    uint8_t tie;
};

typedef struct Tier1CertSource Tier1CertSource;
struct Tier1CertSource
{
    uint64_t immediate;
    uint64_t effect;
    uint32_t opcode;
    Tier1CertOperand operands[BUSTER_CERT_MAX_OPERANDS];
    // Immutable target-derived clobbers, not allocator claims.
    uint32_t before[BUSTER_CERT_MAX_OPERANDS];
    uint32_t after[BUSTER_CERT_MAX_OPERANDS];
    uint8_t use_count;
    uint8_t def_count;
    uint8_t before_count;
    uint8_t after_count;
    bool copy;
};

typedef struct Tier1CertInstruction Tier1CertInstruction;
struct Tier1CertInstruction
{
    uint64_t immediate;
    uint64_t effect;
    uint32_t opcode;
    uint32_t locations[BUSTER_CERT_MAX_OPERANDS];
    // MOVE: a <- b. XCHG: a <-> b. XOR: a <- b xor c.
    // CONSTANT/FRAME_ADDRESS: a <- recipe(value); b is the frame base.
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t value;
    uint8_t operand_count;
    uint8_t op;
};

typedef struct Tier1CertRename Tier1CertRename;
struct Tier1CertRename
{
    uint32_t destination;
    uint32_t source;
};

typedef struct Tier1CertEdge Tier1CertEdge;
struct Tier1CertEdge
{
    const Tier1CertRename* renames;
    const Tier1CertInstruction* moves;
    uint32_t destination;
    uint32_t rename_count;
    uint32_t move_count;
};

typedef struct Tier1CertBlock Tier1CertBlock;
struct Tier1CertBlock
{
    const Tier1CertSource* source;
    const Tier1CertInstruction* code;
    const Tier1CertFact* certificate;
    const Tier1CertEdge* edges;
    uint32_t source_count;
    uint32_t code_count;
    uint32_t certificate_count;
    uint32_t edge_count;
};

typedef struct Tier1CertProgram Tier1CertProgram;
struct Tier1CertProgram
{
    const Tier1CertLocation* locations;
    const Tier1CertValue* values;
    const Tier1CertBlock* blocks;
    const Tier1CertFact* entry;
    const Tier1CertFact* exit;
    uint32_t unit_count;
    uint32_t location_count;
    uint32_t value_count;
    uint32_t block_count;
    uint32_t entry_count;
    uint32_t exit_count;
    uint32_t flags_location;
    uint32_t features;
};

typedef struct Tier1CertReport Tier1CertReport;
struct Tier1CertReport
{
    Tier1CertError error;
    uint32_t block;
    uint32_t edge;
    uint32_t instruction;
    uint64_t instructions;
    uint64_t operands;
    uint64_t edits;
    uint64_t facts;
    uint64_t edges;
    uint64_t renames;
    uint64_t storage_units;
    uint64_t values;
    uint64_t blocks;
    uint64_t unit_reads;
    uint64_t unit_writes;
    uint64_t certificate_bytes;
    uint64_t scratch_bytes;
    uint64_t oracle_rounds;
};

typedef struct Tier1CertMemory Tier1CertMemory;
struct Tier1CertMemory
{
    unsigned char* data;
    size_t capacity;
    size_t used;
    bool failed;
};

static void* tier1_cert_take(Tier1CertMemory* memory, size_t count, size_t size)
{
    void* result = 0;
    const size_t padding = (size_t)(-(uintptr_t)(memory->data + memory->used)) & 15u;
    if (!memory->failed && padding <= memory->capacity - memory->used && (size == 0 || count <= (memory->capacity - memory->used - padding) / size))
    {
        memory->used += padding;
        result = memory->data + memory->used;
        memory->used += count * size;
    }
    else
    {
        memory->failed = true;
    }
    return result;
}

static bool tier1_cert_slice(const void* data, uint32_t count)
{
    return count <= BUSTER_CERT_MAX_RECORDS && (count == 0 || data != 0);
}

static bool tier1_cert_recipe(const Tier1CertProgram* program, uint32_t value)
{
    bool valid = value < program->value_count;
    if (valid)
    {
        const Tier1CertValue v = program->values[value];
        valid = v.class == TIER1_CERT_GPR && (v.width == 4 || v.width == 8);
        if (v.recipe == TIER1_CERT_INTEGER_RECIPE)
        {
            valid = valid && (v.width == 8 || v.payload <= UINT32_MAX);
        }
        else if (v.recipe == TIER1_CERT_FRAME_RECIPE)
        {
            valid = valid && v.width == 8 && v.stable && v.dependency < program->value_count;
            valid = valid && (v.payload <= INT32_MAX || v.payload >= (uint64_t)(int64_t)INT32_MIN);
            if (valid)
            {
                const Tier1CertValue base = program->values[v.dependency];
                valid = base.class == TIER1_CERT_GPR && base.width == 8 && v.dependency != value;
            }
        }
        else
        {
            valid = false;
        }
    }
    return valid;
}

static bool tier1_cert_fact_valid(const Tier1CertProgram* program, Tier1CertFact fact)
{
    bool valid = fact.value < program->value_count;
    if (valid && fact.location == BUSTER_CERT_NONE)
    {
        valid = tier1_cert_recipe(program, fact.value);
    }
    else if (valid)
    {
        valid = fact.location < program->location_count;
        if (valid)
        {
            const Tier1CertLocation location = program->locations[fact.location];
            const Tier1CertValue value = program->values[fact.value];
            valid = location.width == value.width && (location.class == value.class || location.class == TIER1_CERT_STACK);
        }
    }
    return valid;
}

static bool tier1_cert_code_valid(const Tier1CertProgram* program, const Tier1CertInstruction* code, uint32_t count, bool edge)
{
    bool valid = tier1_cert_slice(code, count);
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const Tier1CertInstruction in = code[i];
        valid = in.op <= TIER1_CERT_FRAME_ADDRESS;
        if (in.op == TIER1_CERT_ORIGINAL)
        {
            valid = valid && !edge && in.operand_count <= BUSTER_CERT_MAX_OPERANDS;
            for (uint32_t j = 0; valid && j < in.operand_count; j += 1)
            {
                valid = in.locations[j] < program->location_count;
            }
        }
        else
        {
            valid = valid && in.a < program->location_count && in.operand_count == 0 && in.effect == 0 && in.opcode == 0;
            if (in.op == TIER1_CERT_MOVE || in.op == TIER1_CERT_XCHG || in.op == TIER1_CERT_XOR || in.op == TIER1_CERT_FRAME_ADDRESS)
            {
                valid = valid && in.b < program->location_count;
            }
            if (in.op == TIER1_CERT_XOR)
            {
                valid = valid && in.c < program->location_count;
            }
            if (in.op == TIER1_CERT_CONSTANT || in.op == TIER1_CERT_FRAME_ADDRESS)
            {
                valid = valid && tier1_cert_recipe(program, in.value);
            }
            else
            {
                valid = valid && in.immediate == 0;
            }
        }
    }
    return valid;
}

// Structural validation is shared; the fast proof and slow value-flow transfer
// below are independent. No checker may index a certificate before this pass.
static bool tier1_cert_valid(const Tier1CertProgram* program)
{
    bool valid = program != 0;
    if (valid)
    {
        valid = program->block_count != 0 && program->unit_count <= BUSTER_CERT_MAX_TOTAL;
        valid = valid && tier1_cert_slice(program->locations, program->location_count) && tier1_cert_slice(program->values, program->value_count) && tier1_cert_slice(program->blocks, program->block_count);
        valid = valid && tier1_cert_slice(program->entry, program->entry_count) && tier1_cert_slice(program->exit, program->exit_count);
        valid = valid && (program->flags_location == BUSTER_CERT_NONE || program->flags_location < program->location_count);
    }
    uint64_t total = 0;
    for (uint32_t i = 0; valid && i < program->location_count; i += 1)
    {
        const Tier1CertLocation l = program->locations[i];
        valid = l.width != 0 && l.width <= l.extent && l.extent <= BUSTER_CERT_MAX_WIDTH && l.unit <= program->unit_count && l.extent <= program->unit_count - l.unit;
        valid = valid && l.class <= TIER1_CERT_FLAGS && l.reg < 32;
        if (l.class == TIER1_CERT_GPR)
        {
            valid = valid && l.reg < 16 && (l.width == 1 || l.width == 2 || l.width == 4 || l.width == 8);
            valid = valid && l.extent == (l.width == 4 ? 8 : l.width);
        }
        else if (l.class == TIER1_CERT_VECTOR)
        {
            valid = valid && (l.width == 16 || l.width == 32 || l.width == 64);
        }
        else if (l.class == TIER1_CERT_MASK)
        {
            valid = valid && l.reg < 8 && (l.width == 1 || l.width == 2 || l.width == 4 || l.width == 8);
        }
        else if (l.class == TIER1_CERT_FLAGS)
        {
            valid = valid && l.width == 1 && l.extent == 1;
        }
    }
    if (valid && program->flags_location != BUSTER_CERT_NONE)
    {
        valid = program->locations[program->flags_location].class == TIER1_CERT_FLAGS;
    }
    for (uint32_t i = 0; valid && i < program->value_count; i += 1)
    {
        const Tier1CertValue v = program->values[i];
        valid = v.width != 0 && v.width <= BUSTER_CERT_MAX_WIDTH && v.class <= TIER1_CERT_FLAGS && v.class != TIER1_CERT_STACK;
        valid = valid && (v.recipe == TIER1_CERT_NO_RECIPE || tier1_cert_recipe(program, i));
    }
    for (uint32_t i = 0; valid && i < program->entry_count; i += 1)
    {
        valid = tier1_cert_fact_valid(program, program->entry[i]);
    }
    for (uint32_t i = 0; valid && i < program->exit_count; i += 1)
    {
        valid = tier1_cert_fact_valid(program, program->exit[i]);
    }
    bool sink = false;
    for (uint32_t b = 0; valid && b < program->block_count; b += 1)
    {
        const Tier1CertBlock block = program->blocks[b];
        sink = sink || block.edge_count == 0;
        total += block.source_count + (uint64_t)block.code_count + block.certificate_count + block.edge_count;
        valid = total <= BUSTER_CERT_MAX_TOTAL && tier1_cert_slice(block.source, block.source_count) && tier1_cert_slice(block.certificate, block.certificate_count) && tier1_cert_slice(block.edges, block.edge_count);
        valid = valid && tier1_cert_code_valid(program, block.code, block.code_count, false);
        for (uint32_t i = 0; valid && i < block.certificate_count; i += 1)
        {
            valid = tier1_cert_fact_valid(program, block.certificate[i]);
        }
        for (uint32_t i = 0; valid && i < block.source_count; i += 1)
        {
            const Tier1CertSource source = block.source[i];
            const uint32_t count = source.use_count + source.def_count;
            valid = count <= BUSTER_CERT_MAX_OPERANDS && source.before_count <= BUSTER_CERT_MAX_OPERANDS && source.after_count <= BUSTER_CERT_MAX_OPERANDS;
            valid = valid && (!source.copy || (source.use_count == 1 && source.def_count == 1));
            for (uint32_t o = 0; valid && o < count; o += 1)
            {
                const Tier1CertOperand operand = source.operands[o];
                valid = operand.value < program->value_count && (operand.flags & ~BUSTER_CERT_MEMORY) == 0;
                valid = valid && operand.tie <= (o < source.use_count ? 0 : source.use_count);
                // Distinct output identities are part of the normalized source contract.
                for (uint32_t d = source.use_count; valid && d < o; d += 1)
                {
                    valid = source.operands[d].value != operand.value;
                }
            }
            if (valid && source.copy)
            {
                const Tier1CertValue a = program->values[source.operands[0].value];
                const Tier1CertValue z = program->values[source.operands[1].value];
                valid = a.width == z.width && a.class == z.class;
            }
            for (uint32_t j = 0; valid && j < source.before_count; j += 1)
            {
                valid = source.before[j] < program->location_count;
            }
            for (uint32_t j = 0; valid && j < source.after_count; j += 1)
            {
                valid = source.after[j] < program->location_count;
            }
        }
        for (uint32_t e = 0; valid && e < block.edge_count; e += 1)
        {
            const Tier1CertEdge edge = block.edges[e];
            total += (uint64_t)edge.rename_count + edge.move_count;
            valid = total <= BUSTER_CERT_MAX_TOTAL && edge.destination < program->block_count && tier1_cert_slice(edge.renames, edge.rename_count);
            valid = valid && tier1_cert_code_valid(program, edge.moves, edge.move_count, true);
            for (uint32_t r = 0; valid && r < edge.rename_count; r += 1)
            {
                const Tier1CertRename rename = edge.renames[r];
                valid = rename.source < program->value_count && rename.destination < program->value_count;
                if (valid)
                {
                    const Tier1CertValue s = program->values[rename.source];
                    const Tier1CertValue d = program->values[rename.destination];
                    valid = s.width == d.width && s.class == d.class;
                }
            }
        }
    }
    // Exit requirements must be provable on at least one sink block.
    valid = valid && (program->exit_count == 0 || sink);
    return valid;
}

static bool tier1_cert_disjoint(Tier1CertLocation a, Tier1CertLocation b)
{
    return a.unit + a.extent <= b.unit || b.unit + b.extent <= a.unit;
}

static bool tier1_cert_same(Tier1CertLocation a, Tier1CertLocation b)
{
    return a.unit == b.unit && a.width == b.width && a.extent == b.extent && a.class == b.class && a.reg == b.reg;
}

static bool tier1_cert_operand(const Tier1CertProgram* program, Tier1CertOperand operand, uint32_t location)
{
    const Tier1CertLocation l = program->locations[location];
    const Tier1CertValue v = program->values[operand.value];
    bool valid = l.width == v.width;
    if (l.class == TIER1_CERT_STACK)
    {
        valid = valid && (operand.flags & BUSTER_CERT_MEMORY) != 0;
    }
    else
    {
        valid = valid && l.class == v.class && ((operand.allowed_registers >> l.reg) & 1) != 0;
    }
    return valid;
}

static bool tier1_cert_target(const Tier1CertProgram* program, Tier1CertLocation l)
{
    bool valid = true;
    if (l.class == TIER1_CERT_VECTOR)
    {
        const uint32_t needed = l.width == 64 || l.reg >= 16 ? BUSTER_CERT_AVX512F : l.width == 32 ? BUSTER_CERT_AVX2 : 0;
        // The normalized edit vocabulary does not encode EVEX.128/256 forms.
        valid = (program->features & needed) == needed && (l.reg < 16 || l.width == 64);
    }
    else if (l.class == TIER1_CERT_MASK)
    {
        const uint32_t needed = BUSTER_CERT_AVX512F | (l.width == 1 ? BUSTER_CERT_AVX512DQ : l.width >= 4 ? BUSTER_CERT_AVX512BW : 0);
        valid = (program->features & needed) == needed;
    }
    else
    {
        valid = l.class == TIER1_CERT_GPR || l.class == TIER1_CERT_STACK;
    }
    return valid;
}

static bool tier1_cert_edit(const Tier1CertProgram* program, Tier1CertInstruction in)
{
    const Tier1CertLocation a = program->locations[in.a];
    bool valid = tier1_cert_target(program, a);
    if (in.op == TIER1_CERT_CONSTANT || in.op == TIER1_CERT_FRAME_ADDRESS)
    {
        const Tier1CertValue v = program->values[in.value];
        valid = valid && a.class == TIER1_CERT_GPR && a.width == v.width && in.immediate == v.payload;
        valid = valid && v.recipe == (in.op == TIER1_CERT_CONSTANT ? TIER1_CERT_INTEGER_RECIPE : TIER1_CERT_FRAME_RECIPE);
        if (valid && in.op == TIER1_CERT_FRAME_ADDRESS)
        {
            const Tier1CertLocation base = program->locations[in.b];
            valid = base.class == TIER1_CERT_GPR && base.width == 8;
        }
    }
    else
    {
        const Tier1CertLocation b = program->locations[in.b];
        valid = valid && tier1_cert_target(program, b) && a.width == b.width;
        if (in.op == TIER1_CERT_MOVE)
        {
            valid = valid && (a.class == b.class || a.class == TIER1_CERT_STACK || b.class == TIER1_CERT_STACK);
            valid = valid && !(a.class == TIER1_CERT_STACK && b.class == TIER1_CERT_STACK);
        }
        else if (in.op == TIER1_CERT_XCHG)
        {
            valid = valid && a.class == TIER1_CERT_GPR && b.class == TIER1_CERT_GPR && tier1_cert_disjoint(a, b);
        }
        else if (in.op == TIER1_CERT_XOR)
        {
            const Tier1CertLocation c = program->locations[in.c];
            valid = valid && tier1_cert_target(program, c) && a.width == c.width && a.class == b.class && a.class == c.class && a.class != TIER1_CERT_STACK;
            valid = valid && (a.class != TIER1_CERT_GPR || tier1_cert_same(a, b));
        }
        else
        {
            valid = false;
        }
    }
    return valid;
}

static bool tier1_cert_correspondence(const Tier1CertProgram* program, const Tier1CertSource* source, Tier1CertInstruction in)
{
    const uint32_t count = source->use_count + source->def_count;
    bool valid = in.op == TIER1_CERT_ORIGINAL && in.operand_count == count && in.opcode == source->opcode && in.immediate == source->immediate && in.effect == source->effect;
    for (uint32_t o = 0; valid && o < count; o += 1)
    {
        valid = tier1_cert_operand(program, source->operands[o], in.locations[o]);
        const uint32_t tie = source->operands[o].tie;
        if (valid && tie != 0)
        {
            const Tier1CertLocation a = program->locations[in.locations[o]];
            const Tier1CertLocation b = program->locations[in.locations[tie - 1]];
            valid = a.unit == b.unit && a.width == b.width && a.class == b.class;
        }
        for (uint32_t d = source->use_count; valid && d < o; d += 1)
        {
            valid = tier1_cert_disjoint(program->locations[in.locations[d]], program->locations[in.locations[o]]);
        }
    }
    return valid;
}

typedef struct Tier1CertCell Tier1CertCell;
struct Tier1CertCell
{
    uint32_t token;
    uint32_t stamp;
    uint16_t lane;
    uint16_t width;
};

typedef struct Tier1CertName Tier1CertName;
struct Tier1CertName
{
    uint32_t token;
    uint32_t stamp;
    // The base incarnation a frame recipe was bound to when this name's
    // incarnation was established; zero means no binding was recorded.
    uint32_t base;
};

typedef struct Tier1CertState Tier1CertState;
struct Tier1CertState
{
    const Tier1CertProgram* program;
    Tier1CertCell* base;
    Tier1CertCell* overlay;
    Tier1CertName* names;
    Tier1CertName* renames;
    Tier1CertReport* report;
    uint32_t epoch;
    uint32_t edge_epoch;
    uint32_t next_token;
    bool edge;
};

static Tier1CertCell tier1_cert_read(Tier1CertState* state, uint32_t unit)
{
    Tier1CertCell result = state->base[unit];
    if (result.stamp != state->epoch)
    {
        result = (Tier1CertCell){ 0 };
    }
    if (state->edge && state->overlay[unit].stamp == state->edge_epoch)
    {
        result = state->overlay[unit];
    }
    state->report->unit_reads += 1;
    return result;
}

static void tier1_cert_write(Tier1CertState* state, uint32_t unit, Tier1CertCell value)
{
    if (state->edge)
    {
        value.stamp = state->edge_epoch;
        state->overlay[unit] = value;
    }
    else
    {
        value.stamp = state->epoch;
        state->base[unit] = value;
    }
    state->report->unit_writes += 1;
}

static uint32_t tier1_cert_token(const Tier1CertState* state, uint32_t value)
{
    const Tier1CertName name = state->names[value];
    uint32_t result = name.stamp == state->epoch ? name.token : 0;
    if (result == 0 && tier1_cert_recipe(state->program, value))
    {
        result = value + 1;
    }
    return result;
}

static void tier1_cert_kill(Tier1CertState* state, uint32_t location)
{
    const Tier1CertLocation l = state->program->locations[location];
    for (uint32_t lane = 0; lane < l.extent; lane += 1)
    {
        tier1_cert_write(state, l.unit + lane, (Tier1CertCell){ 0 });
    }
}

static bool tier1_cert_match_token(Tier1CertState* state, uint32_t location, uint32_t token)
{
    const Tier1CertLocation l = state->program->locations[location];
    bool valid = token != 0;
    for (uint32_t lane = 0; valid && lane < l.width; lane += 1)
    {
        const Tier1CertCell cell = tier1_cert_read(state, l.unit + lane);
        valid = cell.token == token && cell.lane == lane && cell.width == l.width;
    }
    return valid;
}

static bool tier1_cert_match(Tier1CertState* state, uint32_t value, uint32_t location)
{
    const uint32_t token = tier1_cert_token(state, value);
    return token != 0 && state->program->locations[location].width == state->program->values[value].width && tier1_cert_match_token(state, location, token);
}

static void tier1_cert_put(Tier1CertState* state, uint32_t location, uint32_t token)
{
    const Tier1CertLocation l = state->program->locations[location];
    tier1_cert_kill(state, location);
    for (uint32_t lane = 0; lane < l.width; lane += 1)
    {
        const Tier1CertCell cell = {
            .token = token,
            .lane = (uint16_t)lane,
            .width = l.width,
        };
        tier1_cert_write(state, l.unit + lane, cell);
    }
}

static bool tier1_cert_seed(Tier1CertState* state, const Tier1CertFact* facts, uint32_t count)
{
    state->epoch += 1;
    state->next_token = state->program->value_count + 1;
    bool valid = true;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const Tier1CertFact fact = facts[i];
        valid = state->names[fact.value].stamp != state->epoch;
        uint32_t token = fact.value + 1;
        if (valid && fact.location != BUSTER_CERT_NONE)
        {
            const Tier1CertLocation l = state->program->locations[fact.location];
            const Tier1CertCell first = tier1_cert_read(state, l.unit);
            if (first.token != 0)
            {
                token = first.token;
            }
            for (uint32_t lane = 0; valid && lane < l.width; lane += 1)
            {
                const Tier1CertCell cell = tier1_cert_read(state, l.unit + lane);
                valid = first.token == 0 ? cell.token == 0 : cell.token == token && cell.lane == lane && cell.width == l.width;
            }
            for (uint32_t lane = 0; valid && lane < l.width; lane += 1)
            {
                const Tier1CertCell cell = {
                    .token = token,
                    .lane = (uint16_t)lane,
                    .width = l.width,
                };
                tier1_cert_write(state, l.unit + lane, cell);
            }
        }
        state->names[fact.value] = (Tier1CertName){
            .token = token,
            .stamp = state->epoch,
        };
    }
    // A frame recipe binds the base incarnation current when the dependent
    // value's incarnation was established, not a later redefinition. Record
    // it only after every fact's name is set so assertion order cannot matter.
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const Tier1CertValue value = state->program->values[facts[i].value];
        if (value.recipe == TIER1_CERT_FRAME_RECIPE)
        {
            state->names[facts[i].value].base = tier1_cert_token(state, value.dependency);
        }
    }
    return valid;
}

static bool tier1_cert_prove(Tier1CertState* state, const Tier1CertFact* facts, uint32_t count, bool rename)
{
    bool valid = true;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const Tier1CertFact fact = facts[i];
        uint32_t value = fact.value;
        if (rename && state->renames[value].stamp == state->edge_epoch)
        {
            value = state->renames[value].token;
        }
        if (fact.location != BUSTER_CERT_NONE)
        {
            valid = tier1_cert_match(state, value, fact.location);
        }
        else
        {
            valid = tier1_cert_recipe(state->program, fact.value);
        }
        state->report->facts += 1;
    }
    return valid;
}

// Exit requirements are assertions like any other: at most one location per
// value, equal-span aliases allowed, partial overlap refused. Entry and
// block-entry certificates get the same discipline from the seed writes; the
// exit list only meets the prover, so its shape is checked explicitly. The
// edge overlay and rename scratch are free outside edge processing.
static bool tier1_cert_exit_shape(Tier1CertState* state)
{
    const Tier1CertProgram* program = state->program;
    state->edge_epoch += 1;
    const uint32_t mark = state->edge_epoch;
    bool valid = true;
    for (uint32_t i = 0; valid && i < program->exit_count; i += 1)
    {
        const Tier1CertFact fact = program->exit[i];
        valid = state->renames[fact.value].stamp != mark;
        state->renames[fact.value] = (Tier1CertName){
            .token = i,
            .stamp = mark,
        };
        if (valid && fact.location != BUSTER_CERT_NONE)
        {
            const Tier1CertLocation l = program->locations[fact.location];
            for (uint32_t lane = 0; valid && lane < l.width; lane += 1)
            {
                Tier1CertCell* const cell = state->overlay + l.unit + lane;
                if (cell->stamp == mark)
                {
                    const Tier1CertLocation other = program->locations[program->exit[cell->token].location];
                    valid = other.unit == l.unit && other.width == l.width;
                }
                else
                {
                    *cell = (Tier1CertCell){
                        .token = i,
                        .stamp = mark,
                    };
                }
            }
        }
    }
    return valid;
}

static bool tier1_cert_copy(Tier1CertState* state, uint32_t destination, uint32_t source, bool exchange)
{
    const Tier1CertLocation a = state->program->locations[destination];
    const Tier1CertLocation b = state->program->locations[source];
    Tier1CertCell left[BUSTER_CERT_MAX_WIDTH];
    Tier1CertCell right[BUSTER_CERT_MAX_WIDTH];
    bool valid = true;
    for (uint32_t i = 0; i < a.width; i += 1)
    {
        right[i] = tier1_cert_read(state, b.unit + i);
        valid = valid && right[i].token != 0;
        if (exchange)
        {
            left[i] = tier1_cert_read(state, a.unit + i);
            valid = valid && left[i].token != 0;
        }
    }
    if (valid)
    {
        tier1_cert_kill(state, destination);
        if (exchange)
        {
            tier1_cert_kill(state, source);
        }
        for (uint32_t i = 0; i < a.width; i += 1)
        {
            tier1_cert_write(state, a.unit + i, right[i]);
            if (exchange)
            {
                tier1_cert_write(state, b.unit + i, left[i]);
            }
        }
    }
    return valid;
}

static bool tier1_cert_original(Tier1CertState* state, const Tier1CertSource* source, Tier1CertInstruction in)
{
    bool valid = tier1_cert_correspondence(state->program, source, in);
    uint32_t copy_token = 0;
    for (uint32_t i = 0; valid && i < source->before_count; i += 1)
    {
        tier1_cert_kill(state, source->before[i]);
    }
    for (uint32_t i = 0; valid && i < source->use_count; i += 1)
    {
        valid = tier1_cert_match(state, source->operands[i].value, in.locations[i]);
        if (i == 0)
        {
            copy_token = tier1_cert_token(state, source->operands[i].value);
        }
    }
    for (uint32_t i = 0; valid && i < source->after_count; i += 1)
    {
        tier1_cert_kill(state, source->after[i]);
    }
    const uint32_t count = source->use_count + source->def_count;
    for (uint32_t i = source->use_count; valid && i < count; i += 1)
    {
        const uint32_t value = source->operands[i].value;
        const uint32_t token = source->copy ? copy_token : state->next_token++;
        const Tier1CertValue defined = state->program->values[value];
        state->names[value] = (Tier1CertName){
            .token = token,
            .stamp = state->epoch,
            .base = defined.recipe == TIER1_CERT_FRAME_RECIPE ? tier1_cert_token(state, defined.dependency) : 0,
        };
        tier1_cert_put(state, in.locations[i], token);
    }
    state->report->instructions += 1;
    state->report->operands += count;
    return valid;
}

static bool tier1_cert_stream(Tier1CertState* state, const Tier1CertSource* source, uint32_t source_count, const Tier1CertInstruction* code, uint32_t count)
{
    bool valid = true;
    uint32_t original = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        state->report->instruction = i;
        const Tier1CertInstruction in = code[i];
        if (in.op == TIER1_CERT_ORIGINAL)
        {
            valid = original < source_count && tier1_cert_correspondence(state->program, source + original, in);
            state->report->error = TIER1_CERT_CORRESPONDENCE;
            if (valid)
            {
                valid = tier1_cert_original(state, source + original, in);
                state->report->error = TIER1_CERT_VALUE;
                original += 1;
            }
        }
        else
        {
            valid = tier1_cert_edit(state->program, in);
            state->report->error = TIER1_CERT_TRANSITION;
            state->report->edits += 1;
            if (valid && in.op == TIER1_CERT_XOR)
            {
                valid = count - i >= 3;
                if (valid)
                {
                    const Tier1CertInstruction middle = code[i + 1];
                    const Tier1CertInstruction last = code[i + 2];
                    valid = middle.op == TIER1_CERT_XOR && last.op == TIER1_CERT_XOR && tier1_cert_edit(state->program, middle) && tier1_cert_edit(state->program, last);
                    const Tier1CertLocation* const locations = state->program->locations;
                    const uint32_t other = tier1_cert_same(locations[in.a], locations[in.b]) ? in.c : tier1_cert_same(locations[in.a], locations[in.c]) ? in.b : BUSTER_CERT_NONE;
                    valid = valid && other != BUSTER_CERT_NONE && tier1_cert_same(locations[middle.a], locations[other]) && tier1_cert_same(locations[last.a], locations[in.a]);
                    valid = valid && ((tier1_cert_same(locations[middle.b], locations[other]) && tier1_cert_same(locations[middle.c], locations[in.a])) || (tier1_cert_same(locations[middle.b], locations[in.a]) && tier1_cert_same(locations[middle.c], locations[other])));
                    valid = valid && ((tier1_cert_same(locations[last.b], locations[in.a]) && tier1_cert_same(locations[last.c], locations[other])) || (tier1_cert_same(locations[last.b], locations[other]) && tier1_cert_same(locations[last.c], locations[in.a])));
                    valid = valid && tier1_cert_disjoint(locations[in.a], locations[other]);
                    if (valid)
                    {
                        valid = tier1_cert_copy(state, in.a, other, true);
                        if (state->program->locations[in.a].class == TIER1_CERT_GPR && state->program->flags_location != BUSTER_CERT_NONE)
                        {
                            tier1_cert_kill(state, state->program->flags_location);
                        }
                        state->report->edits += 2;
                        i += 2;
                    }
                }
            }
            else if (valid && (in.op == TIER1_CERT_MOVE || in.op == TIER1_CERT_XCHG))
            {
                valid = tier1_cert_copy(state, in.a, in.b, in.op == TIER1_CERT_XCHG);
            }
            else if (valid)
            {
                const Tier1CertValue v = state->program->values[in.value];
                if (in.op == TIER1_CERT_FRAME_ADDRESS)
                {
                    Tier1CertName name = state->names[in.value];
                    if (name.stamp != state->epoch)
                    {
                        // The recipe incarnation this edit establishes binds
                        // the base incarnation it is materialized from, so a
                        // later definition of the base cannot retarget it.
                        name = (Tier1CertName){
                            .token = in.value + 1,
                            .stamp = state->epoch,
                            .base = tier1_cert_token(state, v.dependency),
                        };
                        state->names[in.value] = name;
                    }
                    valid = tier1_cert_match_token(state, in.b, name.base);
                }
                if (valid)
                {
                    tier1_cert_put(state, in.a, tier1_cert_token(state, in.value));
                }
            }
        }
    }
    if (valid && original != source_count)
    {
        valid = false;
        state->report->error = TIER1_CERT_CORRESPONDENCE;
    }
    return valid;
}

static bool tier1_cert_reachable(const Tier1CertProgram* program, uint32_t* queue, uint8_t* seen)
{
    const uint32_t count = program->block_count;
    memset(seen, 0, count);
    queue[0] = 0;
    seen[0] = 1;
    uint32_t tail = 1;
    for (uint32_t head = 0; head < tail; head += 1)
    {
        const Tier1CertBlock block = program->blocks[queue[head]];
        for (uint32_t e = 0; e < block.edge_count; e += 1)
        {
            const uint32_t destination = block.edges[e].destination;
            if (!seen[destination])
            {
                seen[destination] = 1;
                queue[tail++] = destination;
            }
        }
    }
    return tail == count;
}

// Upper bound including every alignment gap, including an unaligned buffer.
// This queries only bounded domain sizes; check() still validates all records.
static size_t tier1_certificate_scratch_size(const Tier1CertProgram* program)
{
    size_t result = 0;
    if (program != 0 && program->unit_count <= BUSTER_CERT_MAX_TOTAL && program->value_count <= BUSTER_CERT_MAX_RECORDS && program->block_count != 0 && program->block_count <= BUSTER_CERT_MAX_RECORDS)
    {
        const uint64_t size = 2 * (uint64_t)program->unit_count * sizeof(Tier1CertCell) + 2 * (uint64_t)program->value_count * sizeof(Tier1CertName) + 5 * (uint64_t)program->block_count + 96;
        result = size <= SIZE_MAX ? (size_t)size : 0;
    }
    return result;
}

static Tier1CertError tier1_certificate_check(const Tier1CertProgram* program, void* scratch, size_t capacity, Tier1CertReport* report)
{
    Tier1CertReport result = {
        .error = TIER1_CERT_INVALID,
        .edge = BUSTER_CERT_NONE,
        .instruction = BUSTER_CERT_NONE,
    };
    bool valid = report != 0 && scratch != 0 && tier1_cert_valid(program);
    if (valid)
    {
        const uint32_t units = program->unit_count;
        const uint32_t values = program->value_count;
        const uint32_t blocks = program->block_count;
        result.storage_units = units;
        result.values = values;
        result.blocks = blocks;
        Tier1CertMemory memory = {
            .data = scratch,
            .capacity = capacity,
        };
        Tier1CertState state = {
            .program = program,
            .report = &result,
        };
        state.base = tier1_cert_take(&memory, units, sizeof(*state.base));
        state.overlay = tier1_cert_take(&memory, units, sizeof(*state.overlay));
        state.names = tier1_cert_take(&memory, values, sizeof(*state.names));
        state.renames = tier1_cert_take(&memory, values, sizeof(*state.renames));
        uint32_t* queue = tier1_cert_take(&memory, blocks, sizeof(*queue));
        uint8_t* seen = tier1_cert_take(&memory, blocks, sizeof(*seen));
        result.scratch_bytes = memory.used;
        valid = !memory.failed;
        result.error = TIER1_CERT_CAPACITY;
        if (valid)
        {
            memset(scratch, 0, memory.used);
            valid = tier1_cert_reachable(program, queue, seen);
            result.error = TIER1_CERT_INVALID;
        }
        if (valid)
        {
            valid = tier1_cert_seed(&state, program->entry, program->entry_count);
            valid = valid && tier1_cert_prove(&state, program->blocks[0].certificate, program->blocks[0].certificate_count, false);
            result.error = TIER1_CERT_ENTRY;
        }
        for (uint32_t b = 0; valid && b < blocks; b += 1)
        {
            const Tier1CertBlock block = program->blocks[b];
            result.block = b;
            result.edge = BUSTER_CERT_NONE;
            result.certificate_bytes += (uint64_t)block.certificate_count * sizeof(Tier1CertFact);
            valid = tier1_cert_seed(&state, block.certificate, block.certificate_count);
            result.error = TIER1_CERT_VALUE;
            valid = valid && tier1_cert_stream(&state, block.source, block.source_count, block.code, block.code_count);
            if (valid && block.edge_count == 0)
            {
                valid = tier1_cert_exit_shape(&state);
                valid = valid && tier1_cert_prove(&state, program->exit, program->exit_count, false);
                result.error = TIER1_CERT_VALUE;
            }
            for (uint32_t e = 0; valid && e < block.edge_count; e += 1)
            {
                const Tier1CertEdge edge = block.edges[e];
                const Tier1CertBlock destination = program->blocks[edge.destination];
                result.edge = e;
                result.edges += 1;
                result.renames += edge.rename_count;
                state.edge = true;
                state.edge_epoch += 1;
                for (uint32_t r = 0; valid && r < edge.rename_count; r += 1)
                {
                    const Tier1CertRename rename = edge.renames[r];
                    valid = state.renames[rename.destination].stamp != state.edge_epoch;
                    state.renames[rename.destination] = (Tier1CertName){
                        .token = rename.source,
                        .stamp = state.edge_epoch,
                    };
                }
                valid = valid && tier1_cert_stream(&state, 0, 0, edge.moves, edge.move_count);
                valid = valid && tier1_cert_prove(&state, destination.certificate, destination.certificate_count, true);
                result.error = TIER1_CERT_EDGE;
                state.edge = false;
            }
        }
    }
    if (valid)
    {
        result.error = TIER1_CERT_OK;
    }
    if (report != 0)
    {
        *report = result;
    }
    return result.error;
}
