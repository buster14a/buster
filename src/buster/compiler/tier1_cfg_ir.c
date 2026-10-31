// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// tier1_cfg.c's allocation problems from the shared IR (#140's corpus validity): a promoted function's blocks,
// values, uses, phis and clobbers under a model of x86-64 and the System V convention, until Tier 1's selector makes
// its machine records' problems for general CFGs. Include after ir.c and tier1_cfg.c.
//
// The model:
// - Blocks in the IR's order, which is its layout, and an empty entry before them where the IR's entry has
//   predecessors (Tier1CfgInput's entry has none). A non-phi instruction occupies two positions, its uses' and its
//   result's. A call, an integer division or remainder and a shift by a count that isn't a constant occupy three, the
//   middle one clobbering what the instruction clobbers, so that a value live across it keeps out of those registers:
//   a call the caller-saved general registers and every vector register, a division rax and rdx, a shift rcx. A phi
//   occupies none: it is defined at its block's start.
// - A value per instruction whose result is an integer of up to 64 bits or a pointer (general registers), or a float,
//   a double or a vector of 16, 32 or 64 bytes (vector registers). A function with a 128-bit integer, a long double or
//   a mask value is refused: the two classes don't model them.
// - Constants, symbol and slot addresses and undefined values are rematerializable.
// - Every use but a phi's: one in operand a, or a store's value, needs a register; any other may be a memory operand.
// - Registers: every general register but rsp and r11, the scratch; xmm0 to xmm14, xmm15 the scratch.
//
// This is test and measurement tooling outside the compile path, with no vector path of its own.

#define BUSTER_TIER1_CFG_IR_GPR_REGISTERS 0xf7efu
#define BUSTER_TIER1_CFG_IR_GPR_SCRATCH 11
#define BUSTER_TIER1_CFG_IR_VECTOR_REGISTERS 0x7fffu
#define BUSTER_TIER1_CFG_IR_VECTOR_SCRATCH 15
// rax, rcx, rdx, rsi, rdi, r8, r9 and r10: r11, the other caller-saved one, is the scratch.
#define BUSTER_TIER1_CFG_IR_CALL_CLOBBERS 0x07c7u

typedef enum Tier1CfgIrStatus
{
    TIER1_CFG_IR_OK,
    // A value of a type the model has no class for.
    TIER1_CFG_IR_UNSUPPORTED,
    TIER1_CFG_IR_MEMORY,
    // The IR doesn't verify.
    TIER1_CFG_IR_INVALID,
} Tier1CfgIrStatus;

typedef struct Tier1CfgIrProblem Tier1CfgIrProblem;
struct Tier1CfgIrProblem
{
    Tier1CfgInput input;
    // Each IR instruction's value, or BUSTER_TIER1_CFG_NONE.
    uint32_t* values;
    uint32_t instructions;
    // 1 where the model added an entry before the IR's.
    uint32_t added_entry;
};

// An IR type's class: 0 general, 1 vector, 2 no value, 3 none the model has.
static uint32_t tier1_cfg_ir_class(uint8_t type)
{
    uint32_t class = 3;
    if (type == IR_VOID)
    {
        class = 2;
    }
    else if (type == IR_I1 || type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64 || type == IR_PTR)
    {
        class = 0;
    }
    else if (type == IR_F32 || type == IR_F64 || type == IR_V4I32 || type == IR_V8I32 || type == IR_V16I32)
    {
        class = 1;
    }
    return class;
}

// The general registers instruction i clobbers between its uses and its result, and in *vector the vector ones.
static uint32_t tier1_cfg_ir_clobbers(const IrFunction* function, uint32_t i, uint32_t* vector)
{
    const uint8_t op = function->op[i];
    const bool scalar = tier1_cfg_ir_class(function->type[i]) == 0;
    uint32_t general = 0;
    *vector = 0;
    if (op == IR_CALL || op == IR_CALL_INDIRECT)
    {
        general = BUSTER_TIER1_CFG_IR_CALL_CLOBBERS;
        *vector = BUSTER_TIER1_CFG_IR_VECTOR_REGISTERS;
    }
    else if (scalar && (op == IR_SDIV || op == IR_UDIV || op == IR_SREM || op == IR_UREM))
    {
        general = 0x5u;
    }
    else if (scalar && (op == IR_SHL || op == IR_LSHR || op == IR_ASHR) && function->op[function->b[i]] != IR_CONST)
    {
        general = 0x2u;
    }
    return general;
}

static bool tier1_cfg_ir_rematerializable(uint8_t op)
{
    return op == IR_CONST || op == IR_VCONST || op == IR_UNDEF || op == IR_SYMBOL_ADDR || op == IR_STACK_ADDR;
}

// The allocation problem of a promoted `function`, in `arena`; `scratch` holds the CFG and use lists while it is
// built. On failure both arenas' lengths are as they were.
static Tier1CfgIrStatus tier1_cfg_from_ir(const IrFunction* function, Arena* arena, Arena* scratch, Tier1CfgIrProblem* problem)
{
    const uint64_t arena_saved = arena->length;
    const uint64_t scratch_saved = scratch->length;
    Tier1CfgIrStatus status = TIER1_CFG_IR_INVALID;
    IrCfg cfg = { 0 };
    IrUses uses = { 0 };
    // The CFG and use lists in scratch, their temporaries in the arena, which they give back.
    bool ready = ir_build_cfg(function, scratch, arena, &cfg) == IR_OK && ir_build_uses(function, scratch, arena, &uses) == IR_OK;
    const uint32_t count = function->instruction_count;
    const uint32_t added = ready && cfg.predecessor_offsets[1] != 0 ? 1 : 0;
    const uint32_t blocks = function->block_count + added;
    const uint32_t edges = cfg.edge_count + added;
    // Each instruction's value and positions, and the problem's sizes.
    uint32_t* values = ready ? arena_push_array(arena, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* use_positions_of = ready ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* definitions_of = ready ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* block_starts = ready ? arena_push_array(arena, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    ready = ready && values != 0 && use_positions_of != 0 && definitions_of != 0 && block_starts != 0;
    status = ready ? TIER1_CFG_IR_OK : status;
    uint32_t value_count = 0;
    uint32_t use_count = 0;
    uint32_t phi_count = 0;
    uint32_t operand_count = 0;
    uint32_t fixed_count = 0;
    uint64_t position = added != 0 ? 2 : 0;
    for (uint32_t i = 0; ready && i < count; i += 1)
    {
        const uint32_t class = tier1_cfg_ir_class(function->type[i]);
        status = class == 3 ? TIER1_CFG_IR_UNSUPPORTED : status;
        values[i] = class < 2 ? value_count : BUSTER_TIER1_CFG_NONE;
        value_count += class < 2;
    }
    ready = ready && status == TIER1_CFG_IR_OK;
    if (ready)
    {
        block_starts[0] = 0;
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            block_starts[b + added] = (uint32_t)position;
            for (uint32_t i = function->block_offsets[b]; i < function->block_offsets[b + 1]; i += 1)
            {
                if (function->op[i] == IR_PHI)
                {
                    use_positions_of[i] = BUSTER_TIER1_CFG_NONE;
                    definitions_of[i] = block_starts[b + added];
                    phi_count += values[i] != BUSTER_TIER1_CFG_NONE;
                    operand_count += values[i] != BUSTER_TIER1_CFG_NONE ? function->b[i] : 0;
                }
                else
                {
                    uint32_t vector = 0;
                    const uint32_t general = tier1_cfg_ir_clobbers(function, i, &vector);
                    const uint32_t width = general != 0 || vector != 0 ? 3 : 2;
                    use_positions_of[i] = (uint32_t)position;
                    definitions_of[i] = (uint32_t)position + width - 1;
                    fixed_count += (general != 0) + (vector != 0);
                    position += width;
                }
            }
        }
        block_starts[blocks] = (uint32_t)(position < BUSTER_TIER1_ALLOCATE_MAX_POSITIONS ? position : BUSTER_TIER1_ALLOCATE_MAX_POSITIONS);
        ready = position < BUSTER_TIER1_ALLOCATE_MAX_POSITIONS;
        status = ready ? status : TIER1_CFG_IR_UNSUPPORTED;
    }
    // Ordinary uses: each value's, in instruction and operand order, which is position order.
    for (uint32_t i = 0; ready && i < count; i += 1)
    {
        for (uint32_t u = uses.offsets[i]; values[i] != BUSTER_TIER1_CFG_NONE && u < uses.offsets[i + 1]; u += 1)
        {
            use_count += function->op[uses.uses[u].instruction] != IR_PHI;
        }
    }
    uint8_t* classes = ready ? arena_push(arena, (uint64_t)value_count + 1, 1) : 0;
    uint32_t* allowed = ready ? arena_push_array(arena, (uint64_t)value_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* definitions = ready ? arena_push_array(arena, (uint64_t)value_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint8_t* rematerializable = ready ? arena_push(arena, (uint64_t)value_count + 1, 1) : 0;
    uint32_t* use_offsets = ready ? arena_push_array(arena, (uint64_t)value_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* use_positions = ready ? arena_push_array(arena, (uint64_t)use_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint8_t* use_required = ready ? arena_push(arena, (uint64_t)use_count + 1, 1) : 0;
    uint32_t* successor_offsets = ready ? arena_push_array(arena, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* successors = ready ? arena_push_array(arena, (uint64_t)edges + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* predecessor_offsets = ready ? arena_push_array(arena, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* predecessors = ready ? arena_push_array(arena, (uint64_t)edges + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* phi_offsets = ready ? arena_push_array(arena, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* phi_values = ready ? arena_push_array(arena, (uint64_t)phi_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* phi_operand_offsets = ready ? arena_push_array(arena, (uint64_t)phi_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* phi_operands = ready ? arena_push_array(arena, (uint64_t)operand_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* fixed_starts = ready ? arena_push_array(arena, (uint64_t)fixed_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* fixed_ends = ready ? arena_push_array(arena, (uint64_t)fixed_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint32_t* fixed_registers = ready ? arena_push_array(arena, (uint64_t)fixed_count + 1, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    uint8_t* fixed_classes = ready ? arena_push(arena, (uint64_t)fixed_count + 1, 1) : 0;
    ready = ready && classes != 0 && allowed != 0 && definitions != 0 && rematerializable != 0 && use_offsets != 0 && use_positions != 0 && use_required != 0 && successor_offsets != 0 && successors != 0 && predecessor_offsets != 0 && predecessors != 0 && phi_offsets != 0 && phi_values != 0 && phi_operand_offsets != 0 && phi_operands != 0 && fixed_starts != 0 && fixed_ends != 0 && fixed_registers != 0 && fixed_classes != 0;
    status = ready || status != TIER1_CFG_IR_OK ? status : TIER1_CFG_IR_MEMORY;
    if (ready)
    {
        uint32_t use = 0;
        uint32_t fixed = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t v = values[i];
            if (v != BUSTER_TIER1_CFG_NONE)
            {
                classes[v] = (uint8_t)tier1_cfg_ir_class(function->type[i]);
                allowed[v] = classes[v] == 0 ? BUSTER_TIER1_CFG_IR_GPR_REGISTERS : BUSTER_TIER1_CFG_IR_VECTOR_REGISTERS;
                definitions[v] = definitions_of[i];
                rematerializable[v] = tier1_cfg_ir_rematerializable(function->op[i]);
                use_offsets[v] = use;
                for (uint32_t u = uses.offsets[i]; u < uses.offsets[i + 1]; u += 1)
                {
                    const uint32_t user = uses.uses[u].instruction;
                    if (function->op[user] != IR_PHI)
                    {
                        use_positions[use] = use_positions_of[user];
                        use_required[use] = uses.uses[u].operand == 0 || function->op[user] == IR_STORE;
                        use += 1;
                    }
                }
            }
            uint32_t vector = 0;
            const uint32_t general = function->op[i] != IR_PHI ? tier1_cfg_ir_clobbers(function, i, &vector) : 0;
            for (uint32_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
            {
                const uint32_t clobbered = class == 0 ? general : vector;
                if (clobbered != 0)
                {
                    fixed_starts[fixed] = use_positions_of[i] + 1;
                    fixed_ends[fixed] = use_positions_of[i] + 2;
                    fixed_registers[fixed] = clobbered;
                    fixed_classes[fixed] = (uint8_t)class;
                    fixed += 1;
                }
            }
        }
        use_offsets[value_count] = use;
        // Edges: the added entry's to the IR's, then the IR's, each block one later.
        uint32_t edge = 0;
        successor_offsets[0] = 0;
        if (added != 0)
        {
            successors[0] = 1;
            edge = 1;
        }
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            successor_offsets[b + added] = edge;
            for (uint32_t e = cfg.successor_offsets[b]; e < cfg.successor_offsets[b + 1]; e += 1)
            {
                successors[edge] = cfg.successors[e] + added;
                edge += 1;
            }
        }
        successor_offsets[blocks] = edge;
        edge = 0;
        predecessor_offsets[0] = 0;
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            predecessor_offsets[b + added] = edge;
            if (added != 0 && b == 0)
            {
                predecessors[edge] = 0;
                edge += 1;
            }
            for (uint32_t e = cfg.predecessor_offsets[b]; e < cfg.predecessor_offsets[b + 1]; e += 1)
            {
                predecessors[edge] = cfg.predecessors[e] + added;
                edge += 1;
            }
        }
        predecessor_offsets[blocks] = edge;
        // Phis, each operand in its block's predecessor order: the IR's pairs name each predecessor once.
        uint32_t phi = 0;
        uint32_t operand = 0;
        phi_offsets[0] = 0;
        for (uint32_t b = 0; b < function->block_count; b += 1)
        {
            phi_offsets[b + added] = phi;
            for (uint32_t i = function->block_offsets[b]; i < function->block_offsets[b + 1] && function->op[i] == IR_PHI; i += 1)
            {
                if (values[i] != BUSTER_TIER1_CFG_NONE)
                {
                    phi_values[phi] = values[i];
                    phi_operand_offsets[phi] = operand;
                    for (uint32_t e = cfg.predecessor_offsets[b]; e < cfg.predecessor_offsets[b + 1]; e += 1)
                    {
                        uint32_t incoming = BUSTER_TIER1_CFG_NONE;
                        for (uint32_t pair = 0; pair < function->b[i]; pair += 1)
                        {
                            const uint32_t* entry = function->extra + function->a[i] + (uint64_t)pair * 2;
                            incoming = entry[0] == cfg.predecessors[e] ? entry[1] : incoming;
                        }
                        phi_operands[operand] = incoming != BUSTER_TIER1_CFG_NONE ? values[incoming] : BUSTER_TIER1_CFG_NONE;
                        operand += 1;
                    }
                    phi += 1;
                }
            }
        }
        phi_offsets[blocks] = phi;
        phi_operand_offsets[phi] = operand;
        problem->input = (Tier1CfgInput){
            .block_starts = block_starts,
            .successor_offsets = successor_offsets,
            .successors = successors,
            .predecessor_offsets = predecessor_offsets,
            .predecessors = predecessors,
            .classes = classes,
            .allowed_registers = allowed,
            .definitions = definitions,
            .rematerializable = rematerializable,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = use_required,
            .phi_offsets = phi_offsets,
            .phi_values = phi_values,
            .phi_operand_offsets = phi_operand_offsets,
            .phi_operands = phi_operands,
            .block_count = blocks,
            .edge_count = edges,
            .value_count = value_count,
            .use_count = use,
            .phi_count = phi,
            .position_count = block_starts[blocks],
            .registers = {
                BUSTER_TIER1_CFG_IR_GPR_REGISTERS,
                BUSTER_TIER1_CFG_IR_VECTOR_REGISTERS,
            },
            .scratch_registers = {
                BUSTER_TIER1_CFG_IR_GPR_SCRATCH,
                BUSTER_TIER1_CFG_IR_VECTOR_SCRATCH,
            },
            .fixed = {
                .starts = fixed_starts,
                .ends = fixed_ends,
                .registers = fixed_registers,
                .classes = fixed_classes,
                .count = fixed,
            },
        };
        problem->values = values;
        problem->instructions = count;
        problem->added_entry = added;
    }
    if (status != TIER1_CFG_IR_OK)
    {
        arena_pop_to(arena, arena_saved);
    }
    arena_pop_to(scratch, scratch_saved);
    return status;
}
