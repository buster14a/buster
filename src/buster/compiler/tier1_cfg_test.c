// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier1-cfg: tier1_cfg.c's allocation over CFGs. Include after tier1_cfg.c and dominators.c. A builder states
// blocks of instructions (a use phase and a definition phase each), edges, values, uses, phis and clobbers; fixtures
// give the diamond, the loop, the phi swap, the critical edge and the one-register cases, and the opmask class's
// (#179: a phi swap through k0, a slot swap through its temporary slot, masks across a call and eight masks in seven
// registers), and a generator gives random SSA over random CFGs (loops, irreducible regions, several latches) of the
// three classes, every value defined in a block that dominates its uses. Both entry points must give results that tier1_cfg_verify accepts, and tier1_allocation_verify where the
// case is small enough for its quadratic check (on a slot per spilled value: three generated cases in four give slot
// widths, and their phi webs share slots, #1216). Mutants of the published moves and fragments must fail the checker.
// Every allocation's certificate (#939, tier1_certificate_cfg.c) must pass tier1_certificate_check, and the slow oracle
// must give the same answer wherever its bounds take the program, for the allocations and for every mutant.

#define BUSTER_TIER1_CFG_TEST_BLOCKS 32
#define BUSTER_TIER1_CFG_TEST_EDGES 96
#define BUSTER_TIER1_CFG_TEST_VALUES 512
#define BUSTER_TIER1_CFG_TEST_USES 1024
#define BUSTER_TIER1_CFG_TEST_PHIS 128
#define BUSTER_TIER1_CFG_TEST_FIXED 32
#define BUSTER_TIER1_CFG_TEST_SEQUENCES 64
#define BUSTER_TIER1_CFG_TEST_ARENA (256ull << 20)

typedef struct Tier1CfgTestBuilder Tier1CfgTestBuilder;
struct Tier1CfgTestBuilder
{
    uint32_t block_count;
    uint32_t instructions[BUSTER_TIER1_CFG_TEST_BLOCKS];
    uint32_t edge_count;
    uint32_t edge_from[BUSTER_TIER1_CFG_TEST_EDGES];
    uint32_t edge_to[BUSTER_TIER1_CFG_TEST_EDGES];
    uint32_t value_count;
    uint8_t classes[BUSTER_TIER1_CFG_TEST_VALUES];
    uint32_t allowed[BUSTER_TIER1_CFG_TEST_VALUES];
    uint32_t def_block[BUSTER_TIER1_CFG_TEST_VALUES];
    // UINT32_MAX for a phi, defined at its block's start.
    uint32_t def_instruction[BUSTER_TIER1_CFG_TEST_VALUES];
    uint8_t remat[BUSTER_TIER1_CFG_TEST_VALUES];
    // Each value's slot width, given to the input where share_slots asks for a phi web's shared slots (#1216).
    uint8_t slot_width[BUSTER_TIER1_CFG_TEST_VALUES];
    bool share_slots;
    uint32_t use_count;
    uint32_t use_value[BUSTER_TIER1_CFG_TEST_USES];
    uint32_t use_block[BUSTER_TIER1_CFG_TEST_USES];
    uint32_t use_instruction[BUSTER_TIER1_CFG_TEST_USES];
    uint8_t use_required[BUSTER_TIER1_CFG_TEST_USES];
    // 1 for the definition's own use, at its value's definition position: a required one asks for a register there.
    uint8_t use_definition[BUSTER_TIER1_CFG_TEST_USES];
    uint32_t phi_count;
    uint32_t phi_value[BUSTER_TIER1_CFG_TEST_PHIS];
    // A phi's operand for each of its block's predecessors, in the order the edges list them.
    uint32_t phi_operand[BUSTER_TIER1_CFG_TEST_PHIS][BUSTER_TIER1_CFG_TEST_BLOCKS];
    uint32_t fixed_count;
    uint32_t fixed_block[BUSTER_TIER1_CFG_TEST_FIXED];
    uint32_t fixed_instruction[BUSTER_TIER1_CFG_TEST_FIXED];
    uint8_t fixed_class[BUSTER_TIER1_CFG_TEST_FIXED];
    uint32_t fixed_register[BUSTER_TIER1_CFG_TEST_FIXED];
    // Fixed registers' sequences (#2767), at instructions' definition phases in layout order, their general registers and
    // their readers (a value, or BUSTER_TIER1_CFG_NONE).
    uint32_t sequence_count;
    uint32_t sequence_block[BUSTER_TIER1_CFG_TEST_SEQUENCES];
    uint32_t sequence_instruction[BUSTER_TIER1_CFG_TEST_SEQUENCES];
    uint32_t sequence_registers[BUSTER_TIER1_CFG_TEST_SEQUENCES];
    uint32_t sequence_reader[BUSTER_TIER1_CFG_TEST_SEQUENCES];
    uint32_t sequence_operand[BUSTER_TIER1_CFG_TEST_SEQUENCES];
    uint32_t sequence_result[BUSTER_TIER1_CFG_TEST_SEQUENCES];
    uint32_t registers[BUSTER_TIER1_ALLOCATE_CLASSES];
    uint8_t scratch[BUSTER_TIER1_ALLOCATE_CLASSES];
};

typedef struct Tier1CfgTestTally Tier1CfgTestTally;
struct Tier1CfgTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint64_t allocations;
    uint64_t constraints;
    uint64_t moves;
    uint64_t split_moves;
    uint64_t scratch_moves;
    uint64_t split_edges;
    uint64_t spills;
    uint64_t temp_slot_cases;
    // Block entries where a recomputed value's register reads nothing before its recipe takes over, which its edges
    // then leave unfilled (tier1_cfg_entry_unread, #1217).
    uint64_t unread_entries;
    // Of those, the intervals tier1_cfg_carried_unread proved, which carry the value to blocks that take it as their recipe
    // (#2734), and the mutants that mark one it refused: a block it carries the value to reads its register, which the
    // entry state leaves empty.
    uint64_t carried_entries;
    uint64_t carried_mutants;
    uint64_t carried_killed;
    uint64_t carried_refused;
    // #2611: CFGs whose anchors and chosen predecessors were compared with those read from a given dominator tree, as is
    // and with an entry added before it, and their blocks with a latch among their predecessors.
    uint64_t given_trees;
    uint64_t given_latches;
    // #2767: general ranges that avoid a fixed sequence's registers, and ranges of a sequence's reader across it, which
    // don't avoid that sequence's.
    uint64_t avoiding_ranges;
    uint64_t reading_ranges;
    // The opmask class's share: moves of masks, spilled masks, and moves of masks at splits (around calls).
    uint64_t mask_moves;
    uint64_t mask_spills;
    uint64_t mask_split_moves;
    // Two values given one register where their fragments overlap, and two dependent moves of an edge swapped: every
    // one must fail the checker.
    uint64_t interference_mutants;
    uint64_t interference_killed;
    uint64_t order_mutants;
    uint64_t order_killed;
    // A required definition's register fragment made a slot's: every one must fail the checker.
    uint64_t definition_mutants;
    uint64_t definition_killed;
    // #179's review: a split's move to a register given another register of its class, which leaves the value's piece
    // without it whether or not a use reads the piece, and a mask's register fragment moved to k0, which no mask value
    // may take: every one must fail the checker.
    uint64_t destination_mutants;
    uint64_t destination_killed;
    uint64_t k0_mutants;
    uint64_t k0_killed;
    // #1216: spilled values that share another's slot (a phi web's, tier1_cfg_share_slots), and two values whose slot
    // fragments overlap given one slot: every such mutant must fail the checker.
    uint64_t shared_slots;
    uint64_t slot_mutants;
    uint64_t slot_killed;
    // Phi operands that are undefined (BUSTER_TIER1_CFG_NONE, the IR's undef, #141).
    uint64_t undefined_operands;
    // #1142's review: allocations of a malformed shape (an interval map or an interval's fragments past their columns,
    // a spill fragment where the allocation has no slots, a register left on a fragment without one), which both
    // checkers must refuse before reading through them.
    uint64_t shape_mutants;
    uint64_t shape_killed;
    // #939: allocations whose certificate the checker proved, those the oracle checked too, and the certificates' sizes;
    // the mutants each certificate checker refused, by kind, and where the two checkers judged a mutant apart from
    // tier1_cfg_verify, which checks the allocation's constraints as well as its values.
    uint64_t certificates;
    uint64_t oracle_certificates;
    uint64_t certificate_facts;
    uint64_t certificate_edits;
    uint64_t certificate_operands;
    uint64_t certificate_renames;
    uint64_t interference_refused;
    uint64_t order_refused;
    uint64_t definition_refused;
    uint64_t destination_refused;
    uint64_t k0_refused;
    uint64_t slot_refused;
    uint64_t shape_refused;
    uint64_t certificate_only;
    uint64_t verifier_only;
    // Whether the current case's certificates go to the slow oracle too: every fixture's, and every --oracle-every'th
    // generated case's (16 by default: the dense oracle costs about twenty times the rest of the test), and its
    // mutants' on every fourth of those.
    bool oracle;
    bool oracle_mutants;
    // #2538: plain and preferred problems allocated in one call and in two (tier1_cfg_test_two_problems): the cases whose
    // plain problem allocated, those whose preferred allocation was taken and those whose wasn't, those freed of their
    // definitions' registers by a constraint, and those with fixed ranges, where another problem's column is refused
    // (each then runs without them).
    uint64_t two_cases;
    uint64_t two_taken;
    uint64_t two_refused;
    uint64_t two_freed;
    uint64_t two_fixed;
    uint64_t two_seed;
    // The values whose column sets were probed (tier1_cfg_test_column_sets), and the most in one problem.
    uint64_t column_values;
    uint32_t column_widest;
};

static void tier1_cfg_test_expect(Tier1CfgTestTally* tally, bool condition, const char* message)
{
    tally->checks += 1;
    if (!condition)
    {
        tally->failures += 1;
        if (tally->failures <= 20)
        {
            printf("test-tier1-cfg: %s\n", message);
        }
    }
}

static uint32_t tier1_cfg_test_value(Tier1CfgTestBuilder* builder, uint32_t block, uint32_t instruction, uint8_t class, uint32_t allowed)
{
    const uint32_t v = builder->value_count;
    builder->classes[v] = class;
    builder->allowed[v] = allowed;
    builder->def_block[v] = block;
    builder->def_instruction[v] = instruction;
    builder->remat[v] = 0;
    builder->slot_width[v] = 8;
    builder->value_count += 1;
    return v;
}

static void tier1_cfg_test_use(Tier1CfgTestBuilder* builder, uint32_t value, uint32_t block, uint32_t instruction, bool required)
{
    const uint32_t u = builder->use_count;
    builder->use_value[u] = value;
    builder->use_block[u] = block;
    builder->use_instruction[u] = instruction;
    builder->use_required[u] = required;
    builder->use_definition[u] = 0;
    builder->use_count += 1;
}

// Value v's definition needs a register: a required use at its definition position.
static void tier1_cfg_test_definition_use(Tier1CfgTestBuilder* builder, uint32_t value)
{
    tier1_cfg_test_use(builder, value, builder->def_block[value], 0, true);
    builder->use_definition[builder->use_count - 1] = 1;
}

static uint32_t tier1_cfg_test_phi(Tier1CfgTestBuilder* builder, uint32_t block, uint8_t class, const uint32_t* operands)
{
    const uint32_t v = tier1_cfg_test_value(builder, block, UINT32_MAX, class, builder->registers[class]);
    const uint32_t k = builder->phi_count;
    builder->phi_value[k] = v;
    uint32_t j = 0;
    for (uint32_t e = 0; e < builder->edge_count; e += 1)
    {
        if (builder->edge_to[e] == block)
        {
            builder->phi_operand[k][j] = operands[j];
            j += 1;
        }
    }
    builder->phi_count += 1;
    return v;
}

static void tier1_cfg_test_edge(Tier1CfgTestBuilder* builder, uint32_t from, uint32_t to)
{
    builder->edge_from[builder->edge_count] = from;
    builder->edge_to[builder->edge_count] = to;
    builder->edge_count += 1;
}

// The builder's CFG input, in `arena`: blocks of two positions per instruction, edges sorted by source with
// predecessors in the builder's edge order, uses sorted per value, phis grouped by block.
static bool tier1_cfg_test_build(const Tier1CfgTestBuilder* builder, Arena* arena, Tier1CfgInput* input)
{
    const uint32_t blocks = builder->block_count;
    uint32_t* block_starts = arena_push_array(arena, blocks + 1, sizeof(uint32_t), 4);
    uint32_t* successor_offsets = arena_push_array(arena, blocks + 1, sizeof(uint32_t), 4);
    uint32_t* successors = arena_push_array(arena, builder->edge_count + 1, sizeof(uint32_t), 4);
    uint32_t* predecessor_offsets = arena_push_array(arena, blocks + 1, sizeof(uint32_t), 4);
    uint32_t* predecessors = arena_push_array(arena, builder->edge_count + 1, sizeof(uint32_t), 4);
    uint8_t* classes = arena_push(arena, builder->value_count + 1, 1);
    uint32_t* allowed = arena_push_array(arena, builder->value_count + 1, sizeof(uint32_t), 4);
    uint32_t* definitions = arena_push_array(arena, builder->value_count + 1, sizeof(uint32_t), 4);
    uint8_t* remat = arena_push(arena, builder->value_count + 1, 1);
    uint32_t* use_offsets = arena_push_array(arena, builder->value_count + 1, sizeof(uint32_t), 4);
    uint32_t* use_positions = arena_push_array(arena, builder->use_count + 1, sizeof(uint32_t), 4);
    uint8_t* use_required = arena_push(arena, builder->use_count + 1, 1);
    uint32_t* phi_offsets = arena_push_array(arena, blocks + 1, sizeof(uint32_t), 4);
    uint32_t* phi_values = arena_push_array(arena, builder->phi_count + 1, sizeof(uint32_t), 4);
    uint32_t* phi_operand_offsets = arena_push_array(arena, builder->phi_count + 1, sizeof(uint32_t), 4);
    uint32_t* phi_operands = arena_push_array(arena, (uint64_t)builder->phi_count * BUSTER_TIER1_CFG_TEST_BLOCKS + 1, sizeof(uint32_t), 4);
    uint32_t* fixed_starts = arena_push_array(arena, builder->fixed_count + 1, sizeof(uint32_t), 4);
    uint32_t* fixed_ends = arena_push_array(arena, builder->fixed_count + 1, sizeof(uint32_t), 4);
    uint32_t* fixed_registers = arena_push_array(arena, builder->fixed_count + 1, sizeof(uint32_t), 4);
    uint8_t* fixed_classes = arena_push(arena, builder->fixed_count + 1, 1);
    uint8_t* slot_widths = builder->share_slots ? arena_push(arena, builder->value_count + 1, 1) : 0;
    uint32_t* sequences = arena_push_array(arena, builder->sequence_count + 1, sizeof(uint32_t), 4);
    uint32_t* sequence_registers = arena_push_array(arena, builder->sequence_count + 1, sizeof(uint32_t), 4);
    uint32_t* sequence_readers = arena_push_array(arena, builder->sequence_count + 1, sizeof(uint32_t), 4);
    uint32_t* sequence_operands = arena_push_array(arena, builder->sequence_count + 1, sizeof(uint32_t), 4);
    uint32_t* sequence_results = arena_push_array(arena, builder->sequence_count + 1, sizeof(uint32_t), 4);
    const bool built = (!builder->share_slots || slot_widths != 0) && sequences != 0 && sequence_registers != 0 && sequence_readers != 0 && sequence_operands != 0 && sequence_results != 0 && block_starts != 0 && successor_offsets != 0 && successors != 0 && predecessor_offsets != 0 && predecessors != 0 && classes != 0 && allowed != 0 && definitions != 0 && remat != 0 && use_offsets != 0 && use_positions != 0 && use_required != 0 && phi_offsets != 0 && phi_values != 0 && phi_operand_offsets != 0 && phi_operands != 0 && fixed_starts != 0 && fixed_ends != 0 && fixed_registers != 0 && fixed_classes != 0;
    if (built)
    {
        block_starts[0] = 0;
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            block_starts[b + 1] = block_starts[b] + 2 * builder->instructions[b];
        }
        uint32_t edge = 0;
        uint32_t predecessor = 0;
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            successor_offsets[b] = edge;
            predecessor_offsets[b] = predecessor;
            for (uint32_t e = 0; e < builder->edge_count; e += 1)
            {
                if (builder->edge_from[e] == b)
                {
                    successors[edge++] = builder->edge_to[e];
                }
                if (builder->edge_to[e] == b)
                {
                    predecessors[predecessor++] = builder->edge_from[e];
                }
            }
        }
        successor_offsets[blocks] = edge;
        predecessor_offsets[blocks] = predecessor;
        uint32_t use = 0;
        for (uint32_t v = 0; v < builder->value_count; v += 1)
        {
            const uint32_t start = block_starts[builder->def_block[v]];
            classes[v] = builder->classes[v];
            allowed[v] = builder->allowed[v];
            definitions[v] = builder->def_instruction[v] == UINT32_MAX ? start : start + 2 * builder->def_instruction[v] + 1;
            remat[v] = builder->remat[v];
            if (slot_widths != 0)
            {
                slot_widths[v] = builder->slot_width[v];
            }
            use_offsets[v] = use;
            const uint32_t first = use;
            for (uint32_t u = 0; u < builder->use_count; u += 1)
            {
                if (builder->use_value[u] == v)
                {
                    const uint32_t position = builder->use_definition[u] != 0 ? definitions[v] : block_starts[builder->use_block[u]] + 2 * builder->use_instruction[u];
                    // Insertion keeps the value's positions sorted.
                    uint32_t at = use;
                    while (at > first && use_positions[at - 1] > position)
                    {
                        use_positions[at] = use_positions[at - 1];
                        use_required[at] = use_required[at - 1];
                        at -= 1;
                    }
                    use_positions[at] = position;
                    use_required[at] = builder->use_required[u];
                    use += 1;
                }
            }
        }
        use_offsets[builder->value_count] = use;
        uint32_t phi = 0;
        uint32_t operand = 0;
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            phi_offsets[b] = phi;
            for (uint32_t k = 0; k < builder->phi_count; k += 1)
            {
                if (builder->def_block[builder->phi_value[k]] == b)
                {
                    phi_values[phi] = builder->phi_value[k];
                    phi_operand_offsets[phi] = operand;
                    for (uint32_t j = 0; j < predecessor_offsets[b + 1] - predecessor_offsets[b]; j += 1)
                    {
                        phi_operands[operand++] = builder->phi_operand[k][j];
                    }
                    phi += 1;
                }
            }
        }
        phi_offsets[blocks] = phi;
        phi_operand_offsets[phi] = operand;
        for (uint32_t r = 0; r < builder->fixed_count; r += 1)
        {
            fixed_starts[r] = block_starts[builder->fixed_block[r]] + 2 * builder->fixed_instruction[r] + 1;
            fixed_ends[r] = fixed_starts[r] + 1;
            fixed_classes[r] = builder->fixed_class[r];
            fixed_registers[r] = builder->fixed_register[r];
        }
        for (uint32_t k = 0; k < builder->sequence_count; k += 1)
        {
            sequences[k] = block_starts[builder->sequence_block[k]] + 2 * builder->sequence_instruction[k] + 1;
            sequence_registers[k] = builder->sequence_registers[k];
            sequence_readers[k] = builder->sequence_reader[k];
            sequence_operands[k] = builder->sequence_operand[k];
            sequence_results[k] = builder->sequence_result[k];
        }
        *input = (Tier1CfgInput){
            .block_starts = block_starts,
            .successor_offsets = successor_offsets,
            .successors = successors,
            .predecessor_offsets = predecessor_offsets,
            .predecessors = predecessors,
            .classes = classes,
            .allowed_registers = allowed,
            .definitions = definitions,
            .rematerializable = remat,
            .slot_widths = slot_widths,
            .use_offsets = use_offsets,
            .use_positions = use_positions,
            .use_required = use_required,
            .phi_offsets = phi_offsets,
            .phi_values = phi_values,
            .phi_operand_offsets = phi_operand_offsets,
            .phi_operands = phi_operands,
            .sequences = builder->sequence_count != 0 ? sequences : 0,
            .sequence_registers = builder->sequence_count != 0 ? sequence_registers : 0,
            .sequence_readers = builder->sequence_count != 0 ? sequence_readers : 0,
            .sequence_operands = builder->sequence_count != 0 ? sequence_operands : 0,
            .sequence_results = builder->sequence_count != 0 ? sequence_results : 0,
            .sequence_count = builder->sequence_count,
            .block_count = blocks,
            .edge_count = builder->edge_count,
            .value_count = builder->value_count,
            .use_count = use,
            .phi_count = phi,
            .position_count = block_starts[blocks],
            .registers = {
                builder->registers[BUSTER_TIER1_ALLOCATE_GPR],
                builder->registers[BUSTER_TIER1_ALLOCATE_VECTOR],
                builder->registers[BUSTER_TIER1_ALLOCATE_MASK],
            },
            .scratch_registers = {
                builder->scratch[BUSTER_TIER1_ALLOCATE_GPR],
                builder->scratch[BUSTER_TIER1_ALLOCATE_VECTOR],
                builder->scratch[BUSTER_TIER1_ALLOCATE_MASK],
            },
            .fixed = {
                .starts = fixed_starts,
                .ends = fixed_ends,
                .registers = fixed_registers,
                .classes = fixed_classes,
                .count = builder->fixed_count,
            },
        };
    }
    return built;
}

// The registers each range avoids (#2767), recomputed apart from tier1_cfg_crossed: each general range's, those of every
// sequence strictly inside it whose reader its value isn't, gathered by value with each sequence's operand's and result's,
// then joined along every phi and its operands until nothing changes; each general range must avoid exactly its value's,
// and every other range nothing.
// Without sequences the allocator must have no such column.
static bool tier1_cfg_test_avoided(Tier1CfgTestTally* tally, const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* scratch)
{
    const Tier1AllocationInput* intervals = &result->intervals;
    bool valid = input->sequence_count == 0 ? intervals->avoided_registers == 0 : intervals->avoided_registers != 0;
    const uint64_t saved = scratch->length;
    uint32_t* masks = valid && input->sequence_count != 0 ? arena_push_array(scratch, (uint64_t)input->value_count + 1, sizeof(uint32_t), 4) : 0;
    valid = valid && (input->sequence_count == 0 || masks != 0);
    if (masks != 0)
    {
        memset(masks, 0, ((uint64_t)input->value_count + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < intervals->count; i += 1)
        {
            for (uint32_t k = 0; intervals->classes[i] == BUSTER_TIER1_ALLOCATE_GPR && k < input->sequence_count; k += 1)
            {
                const uint32_t p = input->sequences[k];
                const bool inside = intervals->starts[i] < p && p < intervals->ends[i];
                const bool read = input->sequence_readers != 0 && input->sequence_readers[k] == intervals->values[i];
                masks[intervals->values[i]] |= inside && !read ? input->sequence_registers[k] : 0;
                tally->reading_ranges += inside && read;
            }
        }
        for (uint32_t k = 0; k < input->sequence_count; k += 1)
        {
            const uint32_t operand = input->sequence_operands != 0 ? input->sequence_operands[k] : BUSTER_TIER1_CFG_NONE;
            const uint32_t result = input->sequence_results != 0 ? input->sequence_results[k] : BUSTER_TIER1_CFG_NONE;
            if (operand != BUSTER_TIER1_CFG_NONE)
            {
                masks[operand] |= input->sequence_registers[k];
            }
            if (result != BUSTER_TIER1_CFG_NONE)
            {
                masks[result] |= input->sequence_registers[k];
            }
        }
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (uint32_t k = 0; k < input->phi_count; k += 1)
            {
                const uint32_t phi = input->phi_values[k];
                for (uint32_t o = input->phi_operand_offsets[k]; o < input->phi_operand_offsets[k + 1]; o += 1)
                {
                    const uint32_t operand = input->phi_operands[o];
                    const uint32_t joined = operand != BUSTER_TIER1_CFG_NONE ? masks[phi] | masks[operand] : masks[phi];
                    changed |= joined != masks[phi] || (operand != BUSTER_TIER1_CFG_NONE && joined != masks[operand]);
                    masks[phi] = joined;
                    if (operand != BUSTER_TIER1_CFG_NONE)
                    {
                        masks[operand] = joined;
                    }
                }
            }
        }
        for (uint32_t i = 0; valid && i < intervals->count; i += 1)
        {
            const uint32_t expected = intervals->classes[i] == BUSTER_TIER1_ALLOCATE_GPR ? masks[intervals->values[i]] : 0;
            valid = intervals->avoided_registers[i] == expected;
            tally->avoiding_ranges += expected != 0;
        }
    }
    arena_pop_to(scratch, saved);
    return valid;
}

// The allocation's certificate (#939): published from the input and the allocation, checked by tier1_certificate_check
// and, where its bounds take the program, by the slow oracle, which must agree. Whether the checker proves it.
static bool tier1_cfg_test_certify(Tier1CfgTestTally* tally, const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* output, Arena* scratch, bool count)
{
    const bool oracle = count ? tally->oracle : tally->oracle_mutants;
    const uint64_t output_saved = output->length;
    const uint64_t scratch_saved = scratch->length;
    Tier1CertProgram program = { 0 };
    Tier1CertCfgStats stats = { 0 };
    Tier1CertReport fast = { 0 };
    Tier1CertError error = tier1_certificate_cfg(input, result, output, scratch, &program, &stats);
    if (error == TIER1_CERT_OK)
    {
        const size_t size = tier1_certificate_scratch_size(&program);
        void* buffer = arena_push(scratch, size, 64);
        error = buffer != 0 ? tier1_certificate_check(&program, buffer, size, &fast) : TIER1_CERT_CAPACITY;
        tier1_cfg_test_expect(tally, error != TIER1_CERT_CAPACITY && error != TIER1_CERT_INVALID, "a published certificate is well formed and fits its scratch");
        const size_t oracle_size = oracle ? tier1_certificate_oracle_scratch_size(&program) : 0;
        void* oracle_buffer = oracle_size != 0 ? arena_push(scratch, oracle_size, 64) : 0;
        if (oracle_buffer != 0)
        {
            Tier1CertReport slow = { 0 };
            const Tier1CertError judged = tier1_certificate_oracle(&program, oracle_buffer, oracle_size, &slow);
            tier1_cfg_test_expect(tally, judged != TIER1_CERT_CAPACITY, "the oracle fits the scratch its size gives");
            tier1_cfg_test_expect(tally, (judged == TIER1_CERT_OK) == (error == TIER1_CERT_OK), "the oracle and the certificate checker agree");
            if ((judged == TIER1_CERT_OK) != (error == TIER1_CERT_OK) && tally->failures < 20)
            {
                printf("test-tier1-cfg: certificate checker %u (block %u edge %u instruction %u), oracle %u (block %u edge %u instruction %u)\n", (unsigned)error, fast.block, fast.edge, fast.instruction, (unsigned)judged, slow.block, slow.edge, slow.instruction);
            }
            tally->oracle_certificates += count && judged == TIER1_CERT_OK;
        }
    }
    if (count && error == TIER1_CERT_OK)
    {
        tally->certificates += 1;
        tally->certificate_facts += stats.facts;
        tally->certificate_edits += stats.transitions + stats.split_moves + stats.edge_moves;
        tally->certificate_operands += stats.operands;
        tally->certificate_renames += stats.renames;
    }
    else if (count && tally->failures < 20)
    {
        printf("test-tier1-cfg: certificate error %u (block %u edge %u instruction %u; publication block %u value %u position %u)\n", (unsigned)error, fast.block, fast.edge, fast.instruction, stats.block, stats.value, stats.position);
    }
    arena_pop_to(scratch, scratch_saved);
    arena_pop_to(output, output_saved);
    return error == TIER1_CERT_OK;
}

// One mutant judged by tier1_cfg_verify and by the certificate: whether the verifier rejects it. Their disagreements
// are tallied apart: the verifier also checks constraints (registers outside a value's mask, a fixed range over a
// value the allocation keeps but never reads again) that don't change what any use reads.
static bool tier1_cfg_test_mutant(Tier1CfgTestTally* tally, const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* output, Arena* scratch, uint64_t* refused)
{
    Tier1CfgCheck mutated;
    const bool rejected = !tier1_cfg_verify(input, result, scratch, &mutated);
    const bool certified = tier1_cfg_test_certify(tally, input, result, output, scratch, false);
    *refused += !certified;
    tally->certificate_only += rejected == false && certified == false;
    tally->verifier_only += rejected && certified;
    return rejected;
}

// One malformed shape: both checkers must refuse it before reading through it.
static void tier1_cfg_test_shape(Tier1CfgTestTally* tally, const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* output, Arena* scratch)
{
    tally->shape_killed += tier1_cfg_test_mutant(tally, input, result, output, scratch, &tally->shape_refused);
    tally->shape_mutants += 1;
}

// The block column answers the same intervals and locations as the binary search, at every position and every block entry,
// and the position column, which validation fills (#1399), each position's block.
static bool tier1_cfg_test_lookup(const Tier1CfgInput* input, const Tier1CfgAllocation* result)
{
    const bool expect = result->position_blocks != 0 && input->value_count != 0 && input->block_count != 0 && input->value_count <= BUSTER_TIER1_CFG_BLOCK_INTERVALS / input->block_count;
    bool ok = !expect || result->value_block_base != 0;
    ok = ok && (input->position_count > BUSTER_TIER1_CFG_POSITION_BLOCKS || result->position_blocks != 0);
    for (uint32_t q = 0; ok && result->position_blocks != 0 && q < input->position_count; q += 1)
    {
        ok = result->position_blocks[q] == tier1_cfg_block_of(input, q) && tier1_cfg_block_at(input, result, q) == result->position_blocks[q];
    }
    for (uint32_t v = 0; ok && result->value_block_base != 0 && v < input->value_count; v += 1)
    {
        if (result->value_block_base[v] != BUSTER_TIER1_CFG_NONE)
        {
            for (uint32_t q = 0; ok && q < input->position_count; q += 1)
            {
                uint32_t fast = 0;
                uint32_t slow = 0;
                uint8_t fast_kind = 0;
                uint8_t slow_kind = 0;
                const bool fast_found = tier1_cfg_location(result, v, q, &fast, &fast_kind);
                const bool slow_found = tier1_cfg_location_oracle(result, v, q, &slow, &slow_kind);
                ok = ok && tier1_cfg_interval_at(result, v, q) == tier1_cfg_interval_oracle(result, v, q);
                ok = ok && fast_found == slow_found && (!fast_found || (fast == slow && fast_kind == slow_kind));
            }
            for (uint32_t b = 0; ok && b < input->block_count; b += 1)
            {
                uint32_t fast = 0;
                uint32_t slow = 0;
                uint8_t fast_kind = 0;
                uint8_t slow_kind = 0;
                const bool fast_found = tier1_cfg_entry_location(result, v, input->block_starts[b], &fast, &fast_kind);
                const bool slow_found = tier1_cfg_entry_location_oracle(result, v, input->block_starts[b], &slow, &slow_kind);
                ok = ok && fast_found == slow_found && (!fast_found || (fast == slow && fast_kind == slow_kind));
            }
        }
    }
    return ok;
}

// tier1_allocation_verify on the allocation with a slot for each spilled value, as tier1_allocate gives them: it takes a
// slot to hold one value, and a phi web's shared slots (tier1_cfg_share_slots, #1216) change nothing else it checks.
static bool tier1_cfg_test_unshared_verify(const Tier1CfgInput* input, const Tier1CfgAllocation* result, Arena* scratch)
{
    const uint64_t saved = scratch->length;
    Tier1Allocation unshared = result->allocation;
    uint32_t* ids = arena_push_array(scratch, (uint64_t)input->value_count + 1, sizeof(uint32_t), 4);
    uint32_t* values = arena_push_array(scratch, (uint64_t)input->value_count + 1, sizeof(uint32_t), 4);
    bool valid = ids != 0 && values != 0;
    uint32_t count = 0;
    for (uint32_t v = 0; valid && v < input->value_count; v += 1)
    {
        const bool spilled = result->allocation.spill_ids[v] != UINT32_MAX;
        ids[v] = spilled ? count : UINT32_MAX;
        values[count] = v;
        count += spilled;
    }
    unshared.spill_ids = ids;
    unshared.spill_values = values;
    unshared.spill_count = count;
    valid = valid && tier1_allocation_verify(&result->intervals, &unshared);
    arena_pop_to(scratch, saved);
    return valid;
}

// Allocates one case by both entry points and checks each result. Mutants of the first result with moves: every
// edge move's destination redirected to an unused register, and one interval fragment's register swapped.
// `temp_slots` are the classes whose temporary slot the case must use, `split_moves` the fewest moves at splits it must
// have; a case that `must_allocate` mustn't be refused.
static uint64_t tier1_cfg_test_random(uint64_t* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state >> 33;
}

// #2538: what one case of two problems gave on one path (tier1_cfg_test_two_orders).
typedef struct Tier1CfgTwoOrders Tier1CfgTwoOrders;
struct Tier1CfgTwoOrders
{
    // The two orders' plain verdicts, the problems they left and the allocations that stand agree; the first group of
    // fields that differs where they don't (tier1_cfg_differs), and a failure of the CFG checker on the allocation.
    bool agreed;
    uint32_t differs;
    bool verified;
    // The plain problem allocated; the preferred allocation was taken; the plain verdict freed the definitions.
    bool allocated;
    bool taken;
    bool freed;
};

// A plain problem and a preferred one over the same values, allocated in one call (tier1_cfg_order_prepare and
// tier1_cfg_order_run, as the CFG consumer does, with the plain problem's verdict inside the preferred assignment's call
// or with `verdict` false the plain problem assigned whole first) and in two (tier1_cfg_allocate on each, the order
// before). The preferred problem gives each value `chosen[v]` and its definition's registers while bound,
// and the general class the plain one's registers and `extra`. `allowed` is a column of the values' room.
static Tier1CfgTwoOrders tier1_cfg_test_two_orders(const Tier1CfgInput* plain, const uint32_t* chosen, uint32_t extra, bool verdict, Arena* output, Arena* scratch, uint32_t* allowed)
{
    const uint64_t mark = output->length;
    Tier1CfgTwoOrders outcome = { 0 };
    const uint32_t general = plain->registers[BUSTER_TIER1_ALLOCATE_GPR];
    // Two calls: the plain problem whole, again unbound where it meets a constraint, then the preferred one whole.
    Tier1CfgInput two = *plain;
    Tier1CfgAllocation first = { 0 };
    Tier1CfgError first_error = tier1_cfg_allocate(&two, output, scratch, &first);
    if (first_error == TIER1_CFG_CONSTRAINT)
    {
        two.definition_registers = 0;
        first_error = tier1_cfg_allocate(&two, output, scratch, &first);
    }
    for (uint32_t v = 0; v < plain->value_count; v += 1)
    {
        allowed[v] = chosen[v] | (two.definition_registers != 0 ? two.definition_registers[v] : 0);
    }
    Tier1CfgInput preferred_two = two;
    preferred_two.allowed_registers = allowed;
    preferred_two.registers[BUSTER_TIER1_ALLOCATE_GPR] = general | extra;
    Tier1CfgAllocation second = { 0 };
    const Tier1CfgError second_error = first_error != TIER1_CFG_OK ? TIER1_CFG_INVALID : tier1_cfg_allocate(&preferred_two, output, scratch, &second);
    const bool taken_two = first_error == TIER1_CFG_OK && tier1_cfg_taken(second_error, &second);
    // One call.
    Tier1CfgInput one = *plain;
    Tier1CfgOrder order;
    const Tier1CfgPreference preference = {
        .sets = chosen,
        .registers = {
            general | extra,
            plain->registers[BUSTER_TIER1_ALLOCATE_VECTOR],
            plain->registers[BUSTER_TIER1_ALLOCATE_MASK],
        },
        .verdict = verdict,
    };
    Tier1CfgAllocation result = { 0 };
    const uint32_t* preferred_allowed = 0;
    Tier1CfgError preferred_error = TIER1_CFG_OK;
    bool taken = false;
    Tier1CfgError stands = tier1_cfg_order_prepare(&one, output, scratch, &order);
    stands = stands == TIER1_CFG_OK ? tier1_cfg_order_run(&one, &preference, output, scratch, &order, &result, &preferred_allowed, &preferred_error, &taken) : stands;
    outcome.agreed = stands == first_error && (one.definition_registers == 0) == (two.definition_registers == 0);
    outcome.verified = true;
    if (stands == TIER1_CFG_OK && first_error == TIER1_CFG_OK)
    {
        outcome.differs = tier1_cfg_differs(plain, &result, taken_two ? &second : &first);
        bool same = !taken || preferred_allowed != 0;
        for (uint32_t v = 0; same && taken && v < plain->value_count; v += 1)
        {
            same = preferred_allowed[v] == allowed[v];
        }
        outcome.agreed = outcome.agreed && same && taken == taken_two && preferred_error == second_error && outcome.differs == 0;
        Tier1CfgInput standing = taken ? preferred_two : one;
        Tier1CfgCheck check;
        outcome.verified = tier1_cfg_verify(&standing, &result, scratch, &check);
        outcome.allocated = true;
        outcome.taken = taken;
        outcome.freed = plain->definition_registers != 0 && one.definition_registers == 0;
    }
    arena_pop_to(output, mark);
    return outcome;
}

// Whether value v's definition needs a register: its first use is its own, required. Only such a definition can be bound
// to registers, as a parameter's is to its incoming one (tier1_cfg_verify then checks it is there).
static bool tier1_cfg_test_needs_register(const Tier1CfgInput* input, uint32_t v)
{
    const uint32_t use = input->use_offsets[v];
    return use < input->use_offsets[v + 1] && input->use_positions[use] == input->definitions[v] && input->use_required != 0 && input->use_required[use] != 0;
}

// Binds pairs of values to one register where both need it at once: two values of a class whose definitions need a
// register, used at one position of both definitions' block, both uses required, with a register both may take. Their
// definitions' intervals both hold that register there, which the plain problem can't give both: it meets a constraint,
// and the order frees every definition. `holders` has a column of the positions' room. Returns the pairs bound.
static uint32_t tier1_cfg_test_clash(const Tier1CfgInput* input, uint32_t* holders, uint32_t* defined)
{
    uint32_t pairs = 0;
    memset(holders, 0xff, (uint64_t)input->position_count * sizeof(uint32_t));
    for (uint32_t v = 0; v < input->value_count; v += 1)
    {
        const uint32_t block = tier1_cfg_block_of(input, input->definitions[v]);
        const bool candidate = tier1_cfg_test_needs_register(input, v) && input->allowed_registers[v] != 0;
        for (uint32_t u = input->use_offsets[v] + 1; candidate && defined[v] == 0 && u < input->use_offsets[v + 1]; u += 1)
        {
            const uint32_t position = input->use_positions[u];
            const uint32_t other = holders[position];
            const uint32_t common = other != BUSTER_TIER1_CFG_NONE && other != v && input->classes[other] == input->classes[v] && defined[other] == 0 ? input->allowed_registers[other] & input->allowed_registers[v] : 0;
            const bool here = input->use_required[u] != 0 && tier1_cfg_block_of(input, position) == block;
            if (here && common != 0)
            {
                defined[v] = common & (0u - common);
                defined[other] = defined[v];
                pairs += 1;
            }
            else if (here && other == BUSTER_TIER1_CFG_NONE)
            {
                holders[position] = v;
            }
        }
    }
    return pairs;
}

// The preferred problem of a case of two problems: the general class widened by `extra`, a random part of the general
// registers the plain problem leaves (never the scratch), and each general value's set, at random, its plain one, `extra`
// alone or both; in about one case of four, one value with a required use gets no register at all, so that the preferred
// problem meets a constraint and the plain allocation stands. `bind` binds definitions (`defined`): 1, about half the
// values whose definitions need a register, each to one register of its own, as a parameter's is to its incoming
// register; 2, pairs that clash (tier1_cfg_test_clash). Returns `extra`.
static uint32_t tier1_cfg_test_preference(const Tier1CfgInput* input, uint64_t* random, uint32_t bind, uint32_t* chosen, uint32_t* defined, uint32_t* holders)
{
    const uint32_t spare = 0xffffu & ~input->registers[BUSTER_TIER1_ALLOCATE_GPR] & ~(UINT32_C(1) << input->scratch_registers[BUSTER_TIER1_ALLOCATE_GPR]);
    uint32_t extra = 0;
    for (uint32_t r = 0; r < 16; r += 1)
    {
        extra |= ((spare >> r) & 1) != 0 && tier1_cfg_test_random(random) % 2 == 0 ? UINT32_C(1) << r : 0;
    }
    const bool starve = tier1_cfg_test_random(random) % 4 == 0;
    bool starved = false;
    for (uint32_t v = 0; v < input->value_count; v += 1)
    {
        const uint32_t own = input->allowed_registers[v];
        const uint32_t pick = (uint32_t)(tier1_cfg_test_random(random) % 3);
        const bool required = input->use_required != 0 && input->use_offsets[v] < input->use_offsets[v + 1] && input->use_required[input->use_offsets[v]] != 0;
        const bool none = starve && !starved && required && input->classes[v] == BUSTER_TIER1_ALLOCATE_GPR;
        starved = starved || none;
        chosen[v] = input->classes[v] != BUSTER_TIER1_ALLOCATE_GPR ? own : none ? 0 : pick == 0 ? own : pick == 1 ? extra : own | extra;
        uint32_t bit = 0;
        if (bind == 1 && tier1_cfg_test_needs_register(input, v) && own != 0 && tier1_cfg_test_random(random) % 2 == 0)
        {
            const uint32_t skip = (uint32_t)(tier1_cfg_test_random(random) % (uint32_t)__builtin_popcount(own));
            uint32_t rest = own;
            for (uint32_t k = 0; k < skip; k += 1)
            {
                rest &= rest - 1;
            }
            bit = rest & (0u - rest);
        }
        defined[v] = bit;
    }
    if (bind == 2)
    {
        tier1_cfg_test_clash(input, holders, defined);
    }
    return extra;
}

// `input` with a required use at each definition, a phi's aside, ahead of the value's other uses at that position: then a
// definition needs a register and can be bound to one (tier1_cfg_test_preference). Real C's problems
// (tier1_cfg_ir.c's) have no use at a definition. False where the arena is short.
static bool tier1_cfg_test_definition_uses(const Tier1CfgInput* input, Arena* arena, Tier1CfgInput* result)
{
    const uint32_t values = input->value_count;
    uint8_t* phi = arena_push(arena, (uint64_t)values + 1, 1);
    uint32_t* offsets = arena_push_array(arena, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* positions = arena_push_array(arena, (uint64_t)input->use_count + values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint8_t* required = arena_push(arena, (uint64_t)input->use_count + values + 1, 1);
    const bool ready = phi != 0 && offsets != 0 && positions != 0 && required != 0;
    if (ready)
    {
        memset(phi, 0, (uint64_t)values + 1);
        for (uint32_t k = 0; k < input->phi_count; k += 1)
        {
            phi[input->phi_values[k]] = 1;
        }
        uint32_t count = 0;
        for (uint32_t v = 0; v < values; v += 1)
        {
            offsets[v] = count;
            bool added = phi[v] != 0;
            for (uint32_t u = input->use_offsets[v]; u <= input->use_offsets[v + 1]; u += 1)
            {
                const bool last = u == input->use_offsets[v + 1];
                if (!added && (last || input->use_positions[u] >= input->definitions[v]))
                {
                    positions[count] = input->definitions[v];
                    required[count] = 1;
                    count += 1;
                    added = true;
                }
                if (!last)
                {
                    positions[count] = input->use_positions[u];
                    required[count] = input->use_required != 0 ? input->use_required[u] : 0;
                    count += 1;
                }
            }
        }
        offsets[values] = count;
        *result = *input;
        result->use_offsets = offsets;
        result->use_positions = positions;
        result->use_required = required;
        result->use_count = count;
    }
    return ready;
}

// The column's set checks (tier1_cfg_column, tier1_cfg_input_valid's two conditions on a value's registers), value by
// value for the first 48: over `plain`'s prepared intervals, the preferred problem's own column stands; a register
// outside its class's added to one value's set is refused, and so is one value's definition bound to a register outside
// its set, every other definition as the preferred problem binds it. Counts the values probed into `tally`.
static void tier1_cfg_test_column_sets(Tier1CfgTestTally* tally, const Tier1CfgInput* plain, const uint32_t* chosen, uint32_t extra, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    const Tier1CfgPreference preference = {
        .sets = chosen,
        .registers = {
            plain->registers[BUSTER_TIER1_ALLOCATE_GPR] | extra,
            plain->registers[BUSTER_TIER1_ALLOCATE_VECTOR],
            plain->registers[BUSTER_TIER1_ALLOCATE_MASK],
        },
    };
    const uint32_t values = plain->value_count;
    Tier1CfgInput preferred = { 0 };
    Tier1CfgPrepared prepared = { 0 };
    uint32_t* column = 0;
    uint32_t* sets = arena_push_array(output, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* bound = arena_push_array(output, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    const bool built = sets != 0 && bound != 0 && tier1_cfg_preferred(plain, &preference, output, &preferred);
    const bool held = built && tier1_cfg_prepare(plain, output, scratch, &prepared) == TIER1_CFG_OK;
    tier1_cfg_test_expect(tally, !held || tier1_cfg_column(&preferred, &prepared, output, &column) == TIER1_CFG_OK, "the preferred problem's column stands");
    bool outside = true;
    bool unallowed = true;
    for (uint32_t v = 0; held && v < values; v += 1)
    {
        sets[v] = preferred.allowed_registers[v];
        bound[v] = preferred.definition_registers != 0 ? preferred.definition_registers[v] : 0;
    }
    for (uint32_t k = 0; held && k < values && k < 48; k += 1)
    {
        Tier1CfgInput probe = preferred;
        probe.allowed_registers = sets;
        probe.definition_registers = bound;
        const uint32_t registers = preferred.registers[preferred.classes[k]];
        const uint32_t set = sets[k];
        const uint32_t definition = bound[k];
        // The lowest register outside the class's, and the lowest outside the value's set.
        const uint32_t foreign = ~registers & (registers + 1);
        const uint32_t loose = ~set & (set + 1);
        sets[k] = set | foreign;
        outside = outside && (foreign == 0 || tier1_cfg_column(&probe, &prepared, output, &column) == TIER1_CFG_INVALID);
        sets[k] = set;
        bound[k] = loose;
        unallowed = unallowed && (loose == 0 || tier1_cfg_column(&probe, &prepared, output, &column) == TIER1_CFG_INVALID);
        bound[k] = definition;
        tally->column_values += 1;
        tally->column_widest = k + 1 > tally->column_widest ? k + 1 : tally->column_widest;
    }
    tier1_cfg_test_expect(tally, outside, "the column refuses a register outside its class's, value by value");
    tier1_cfg_test_expect(tally, unallowed, "the column refuses a definition bound outside its value's set, value by value");
    if (held)
    {
        tier1_cfg_release(&prepared, scratch);
    }
    arena_pop_to(output, mark);
}

// #2538: a case of two problems (tier1_cfg_test_two_orders) on both paths, with a random preference and binding
// (tier1_cfg_test_preference). Where the input has fixed ranges the intervals depend on the registers: then
// tier1_cfg_column must refuse another problem, as the CFG consumer takes the order before, and the case runs without
// them.
static void tier1_cfg_test_two_problems(Tier1CfgTestTally* tally, const Tier1CfgInput* input, Arena* output, Arena* scratch, uint32_t bind)
{
    const uint64_t mark = output->length;
    uint64_t random = tally->two_seed * UINT64_C(0x9e3779b97f4a7c15) + 1;
    tally->two_seed += 1;
    const uint32_t values = input->value_count;
    uint32_t* defined = arena_push_array(output, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* chosen = arena_push_array(output, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* allowed = arena_push_array(output, (uint64_t)values + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* holders = arena_push_array(output, (uint64_t)input->position_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    const bool ready = defined != 0 && chosen != 0 && allowed != 0 && holders != 0;
    tier1_cfg_test_expect(tally, ready, "two problems' columns");
    const uint32_t extra = ready ? tier1_cfg_test_preference(input, &random, bind, chosen, defined, holders) : 0;
    Tier1CfgInput plain = *input;
    plain.definition_registers = bind != 0 ? defined : 0;
    if (ready && input->fixed.count != 0)
    {
        Tier1CfgPrepared prepared = { 0 };
        uint32_t* column = 0;
        const bool held = tier1_cfg_prepare(&plain, output, scratch, &prepared) == TIER1_CFG_OK;
        tier1_cfg_test_expect(tally, !held || tier1_cfg_column(&plain, &prepared, output, &column) == TIER1_CFG_INVALID, "another problem's column is refused where fixed ranges cut the intervals");
        if (held)
        {
            tier1_cfg_release(&prepared, scratch);
        }
        tally->two_fixed += 1;
        plain.fixed = (Tier1FixedIntervals){ 0 };
    }
    if (ready)
    {
        tier1_cfg_test_column_sets(tally, &plain, chosen, extra, output, scratch);
    }
    // With the plain problem's verdict inside the preferred assignment's call and with it assigned whole first.
    for (uint32_t run = 0; ready && run < 2; run += 1)
    {
        const bool verdict = run == 0;
        const Tier1CfgTwoOrders outcome = tier1_cfg_test_two_orders(&plain, chosen, extra, verdict, output, scratch, allowed);
        if (!outcome.agreed && tally->failures < 20)
        {
            printf("test-tier1-cfg: two problems, %s, bound=%u: the orders disagree (fields %u)\n", verdict ? "verdict inside" : "plain first", bind, outcome.differs);
        }
        tier1_cfg_test_expect(tally, outcome.agreed, "one call's verdict, problem and allocation are two calls'");
        tier1_cfg_test_expect(tally, outcome.verified, "the CFG checker accepts the allocation that stands");
        tally->two_cases += run == 0 && outcome.allocated;
        tally->two_taken += run == 0 && outcome.taken;
        tally->two_refused += run == 0 && outcome.allocated && !outcome.taken;
        tally->two_freed += run == 0 && outcome.freed;
    }
    arena_pop_to(output, mark);
}

// A block's first successor in layout, or BUSTER_TIER1_CFG_NONE.
static uint32_t tier1_cfg_test_first_successor(const Tier1CfgInput* input, uint32_t b)
{
    uint32_t first = BUSTER_TIER1_CFG_NONE;
    for (uint32_t s = input->successor_offsets[b]; s < input->successor_offsets[b + 1]; s += 1)
    {
        first = input->successors[s] < first ? input->successors[s] : first;
    }
    return first;
}

// The range that interval i's block c gives as its continuation (#2774), from the intervals alone, where i leaves c at its
// end: c's first successor in layout, where the value enters it (a range of it covers that block's start, which isn't its
// definition's block) and i doesn't go on into it as the next block; there, the range starting at that block's start
// where that block comes no later than c; else the range covering the start of the block whose entry it takes its
// opening from: its predecessors' first successor where no earlier block reaches it, else its own first successor where
// that comes before it, which must come no later than c, and not be the value's definition's.
static uint32_t tier1_cfg_test_continuation(const Tier1CfgInput* input, const Tier1CfgAllocation* result, uint32_t i, uint32_t c)
{
    const Tier1AllocationInput* intervals = &result->intervals;
    const uint32_t v = intervals->values[i];
    const uint32_t home = tier1_cfg_block_of(input, input->definitions[v]);
    const bool goes_on = intervals->ends[i] > input->block_starts[c + 1];
    const uint32_t first = tier1_cfg_test_first_successor(input, c);
    const uint32_t next = goes_on && first == c + 1 ? BUSTER_TIER1_CFG_NONE : first;
    const bool enters = next != BUSTER_TIER1_CFG_NONE && home != next && tier1_cfg_interval_oracle(result, v, input->block_starts[next]) != BUSTER_TIER1_CFG_NONE;
    uint32_t model = BUSTER_TIER1_CFG_NONE;
    bool reached = false;
    for (uint32_t p = enters && next > c ? input->predecessor_offsets[next] : 0; enters && next > c && p < input->predecessor_offsets[next + 1]; p += 1)
    {
        const uint32_t sibling = tier1_cfg_test_first_successor(input, input->predecessors[p]);
        reached |= input->predecessors[p] < next;
        model = sibling < next && sibling < model ? sibling : model;
    }
    const uint32_t joined = enters && next > c ? tier1_cfg_test_first_successor(input, next) : BUSTER_TIER1_CFG_NONE;
    model = reached || model == BUSTER_TIER1_CFG_NONE ? (joined < next ? joined : BUSTER_TIER1_CFG_NONE) : model;
    const uint32_t taken = !enters ? BUSTER_TIER1_CFG_NONE : next <= c ? next : model;
    const uint32_t entry = taken <= c && home != taken ? tier1_cfg_interval_oracle(result, v, input->block_starts[taken]) : BUSTER_TIER1_CFG_NONE;
    return entry != BUSTER_TIER1_CFG_NONE && (next > c || intervals->starts[entry] == input->block_starts[taken]) ? entry : BUSTER_TIER1_CFG_NONE;
}

// #2774: each interval's continuation, recomputed from the intervals alone: the last that the blocks it leaves at their
// ends give (tier1_cfg_test_continuation), or none.
static bool tier1_cfg_test_continuations(const Tier1CfgInput* input, const Tier1CfgAllocation* result)
{
    const Tier1AllocationInput* intervals = &result->intervals;
    bool same = intervals->continuation_intervals != 0;
    for (uint32_t i = 0; same && i < intervals->count; i += 1)
    {
        const uint32_t from = tier1_cfg_block_of(input, intervals->starts[i]);
        const uint32_t to = tier1_cfg_block_of(input, intervals->ends[i] - 1);
        uint32_t expected = BUSTER_TIER1_CFG_NONE;
        for (uint32_t c = from; c <= to; c += 1)
        {
            const bool leaves = c < to || intervals->ends[i] == input->block_starts[c + 1];
            const uint32_t given = leaves ? tier1_cfg_test_continuation(input, result, i, c) : BUSTER_TIER1_CFG_NONE;
            expected = given != BUSTER_TIER1_CFG_NONE ? given : expected;
        }
        same = intervals->continuation_intervals[i] == expected;
    }
    return same;
}

static void tier1_cfg_test_case(Tier1CfgTestTally* tally, const Tier1CfgInput* input, Arena* output, Arena* scratch, const char* name, bool mutate, uint8_t temp_slots, uint32_t split_moves, bool must_allocate)
{
    const uint64_t output_saved = output->length;
    Tier1CfgAllocation result = { 0 };
    const Tier1CfgError error = tier1_cfg_allocate(input, output, scratch, &result);
    for (uint32_t o = 0; o < input->phi_operand_offsets[input->phi_count]; o += 1)
    {
        tally->undefined_operands += input->phi_operands[o] == BUSTER_TIER1_CFG_NONE;
    }
    tier1_cfg_test_expect(tally, error == TIER1_CFG_OK || error == TIER1_CFG_CONSTRAINT, name);
    tier1_cfg_test_expect(tally, error == TIER1_CFG_OK || output->length == output_saved, "a failed allocation restores the output arena");
    if (error == TIER1_CFG_OK)
    {
        tally->allocations += 1;
        tally->moves += result.move_count;
        tally->split_moves += result.move_count - result.edge_move_offsets[input->edge_count];
        tally->split_edges += result.split_edges;
        tally->spills += result.allocation.spill_count;
        for (uint32_t v = 0; v < input->value_count; v += 1)
        {
            tally->shared_slots += result.allocation.spill_ids[v] != UINT32_MAX;
        }
        tally->shared_slots -= result.allocation.spill_count;
        Tier1CfgCheck check;
        const bool verified = tier1_cfg_verify(input, &result, scratch, &check);
        if (!verified && tally->failures < 20)
        {
            printf("test-tier1-cfg: %s: %s (block %u value %u position %u)\n", name, check.reason, check.block, check.value, check.position);
        }
        tier1_cfg_test_expect(tally, verified, "the CFG checker accepts the allocation");
        tier1_cfg_test_expect(tally, tier1_cfg_test_lookup(input, &result), "block intervals match the binary search");
        tier1_cfg_test_expect(tally, tier1_cfg_test_continuations(input, &result), "each live-out range names its value's range at its first successor's entry");
        tier1_cfg_test_expect(tally, tier1_cfg_test_certify(tally, input, &result, output, scratch, true), "the certificate checker proves the allocation");
        tier1_cfg_test_expect(tally, tier1_cfg_test_avoided(tally, input, &result, scratch), "each range avoids its phi web's fixed sequences' registers, but its own readings'");
        if (result.intervals.count <= 600)
        {
            tier1_cfg_test_expect(tally, tier1_cfg_test_unshared_verify(input, &result, scratch), "the interval verifier accepts the allocation");
        }
        for (uint32_t m = 0; m < result.move_count; m += 1)
        {
            const uint32_t class = input->classes[result.move_values[m]];
            const uint32_t conduit = class * 32 + input->scratch_registers[class];
            tally->scratch_moves += result.move_sources[m] == conduit || result.move_destinations[m] == conduit;
            tally->mask_moves += class == BUSTER_TIER1_ALLOCATE_MASK;
            tally->mask_split_moves += class == BUSTER_TIER1_ALLOCATE_MASK && m >= result.edge_move_offsets[input->edge_count];
        }
        for (uint32_t k = 0; k < result.allocation.spill_count; k += 1)
        {
            tally->mask_spills += input->classes[result.allocation.spill_values[k]] == BUSTER_TIER1_ALLOCATE_MASK;
        }
        tally->temp_slot_cases += result.temp_slots != 0;
        for (uint32_t b = 0; b < input->block_count; b += 1)
        {
            const uint32_t entry = input->block_starts[b];
            for (uint32_t v = 0; v < input->value_count; v += 1)
            {
                uint32_t location = 0;
                uint8_t kind = 0;
                const uint32_t interval = tier1_cfg_interval_oracle(&result, v, entry);
                const uint32_t position = interval != BUSTER_TIER1_CFG_NONE && result.intervals.starts[interval] < entry ? entry - 1 : entry;
                const uint32_t fragment = interval != BUSTER_TIER1_CFG_NONE ? tier1_cfg_interval_fragment(&result, interval, position) : BUSTER_TIER1_CFG_NONE;
                const bool found = fragment != BUSTER_TIER1_CFG_NONE && tier1_cfg_entry_location(&result, v, entry, &location, &kind);
                tally->unread_entries += found && kind == BUSTER_TIER1_ALLOCATE_REMATERIALIZE && result.allocation.kinds[fragment] == BUSTER_TIER1_ALLOCATE_REGISTER;
            }
        }
        for (uint32_t i = 0; result.unread_entries != 0 && i < result.intervals.count; i += 1)
        {
            tally->carried_entries += result.unread_entries[i] != 0;
        }
        tier1_cfg_test_expect(tally, (result.temp_slots & temp_slots) == temp_slots, "the case uses its temporary slots");
        tier1_cfg_test_expect(tally, result.move_count - result.edge_move_offsets[input->edge_count] >= split_moves, "the case moves values at its splits");
        if (mutate)
        {
            Tier1Allocation* allocation = &result.allocation;
            // Interference: a register fragment given the register of another value's register fragment that
            // overlaps it. The later of the two writes then lands on a value the allocation keeps there.
            uint32_t tried = 0;
            for (uint32_t i = 0; tried < 8 && i < result.intervals.count; i += 1)
            {
                for (uint32_t f = allocation->fragment_offsets[i]; tried < 8 && f < allocation->fragment_offsets[i + 1]; f += 1)
                {
                    uint32_t other = UINT32_MAX;
                    for (uint32_t j = 0; allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && other == UINT32_MAX && j < result.intervals.count; j += 1)
                    {
                        const bool apart = result.intervals.values[j] != result.intervals.values[i] && result.intervals.classes[j] == result.intervals.classes[i];
                        for (uint32_t g = allocation->fragment_offsets[j]; apart && other == UINT32_MAX && g < allocation->fragment_offsets[j + 1]; g += 1)
                        {
                            const bool overlap = allocation->starts[g] < allocation->ends[f] && allocation->starts[f] < allocation->ends[g];
                            // A spill or rematerialize fragment has no register: its field is a slot or byte count, not
                            // a bit allowed_registers can hold, so the shift waits for the kind to say it is a register.
                            const bool registered = allocation->kinds[g] == BUSTER_TIER1_ALLOCATE_REGISTER;
                            const bool allowed = registered && allocation->registers[g] < 32 && ((result.intervals.allowed_registers[i] >> allocation->registers[g]) & 1) != 0;
                            if (registered && overlap && allowed && allocation->registers[g] != allocation->registers[f])
                            {
                                other = g;
                            }
                        }
                    }
                    if (other != UINT32_MAX)
                    {
                        const uint8_t saved = allocation->registers[f];
                        allocation->registers[f] = allocation->registers[other];
                        const bool rejected = tier1_cfg_test_mutant(tally, input, &result, output, scratch, &tally->interference_refused);
                        allocation->registers[f] = saved;
                        tally->interference_mutants += 1;
                        tally->interference_killed += rejected;
                        tried += 1;
                    }
                }
            }
            // Slot: a spilled value given the slot of another value of its class whose slot fragment overlaps one of
            // its own (#1216). Whichever reaches the slot second overwrites the other while the allocation keeps it there.
            tried = 0;
            for (uint32_t i = 0; tried < 4 && i < result.intervals.count; i += 1)
            {
                const uint32_t v = result.intervals.values[i];
                for (uint32_t f = allocation->fragment_offsets[i]; tried < 4 && f < allocation->fragment_offsets[i + 1]; f += 1)
                {
                    uint32_t other = UINT32_MAX;
                    for (uint32_t j = 0; allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_SPILL && other == UINT32_MAX && j < result.intervals.count; j += 1)
                    {
                        const uint32_t w = result.intervals.values[j];
                        const bool apart = w != v && result.intervals.classes[j] == result.intervals.classes[i] && allocation->spill_ids[w] != allocation->spill_ids[v];
                        for (uint32_t g = allocation->fragment_offsets[j]; apart && other == UINT32_MAX && g < allocation->fragment_offsets[j + 1]; g += 1)
                        {
                            const bool overlap = allocation->starts[g] < allocation->ends[f] && allocation->starts[f] < allocation->ends[g];
                            other = overlap && allocation->kinds[g] == BUSTER_TIER1_ALLOCATE_SPILL ? w : other;
                        }
                    }
                    if (other != UINT32_MAX)
                    {
                        const uint32_t saved = allocation->spill_ids[v];
                        allocation->spill_ids[v] = allocation->spill_ids[other];
                        const bool rejected = tier1_cfg_test_mutant(tally, input, &result, output, scratch, &tally->slot_refused);
                        allocation->spill_ids[v] = saved;
                        tally->slot_mutants += 1;
                        tally->slot_killed += rejected;
                        tried += 1;
                    }
                }
            }
            // #2734: an interval of the carried proof's shape that it refused, marked as proven: a recomputed value's one
            // register fragment, unused, over whole blocks. A block it carries the value to reads the register, which
            // control now enters without.
            tried = 0;
            for (uint32_t i = 0; result.unread_entries != 0 && result.intervals.rematerializable != 0 && tried < 4 && i < result.intervals.count; i += 1)
            {
                const uint32_t f = allocation->fragment_offsets[i];
                const uint32_t first = tier1_cfg_block_of(input, result.intervals.starts[i]);
                const uint32_t last = tier1_cfg_block_of(input, result.intervals.ends[i] - 1);
                const bool shaped = result.unread_entries[i] == 0 && result.intervals.rematerializable[i] != 0 && allocation->fragment_offsets[i + 1] - f == 1 &&
                                    allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && result.intervals.use_offsets[i] == result.intervals.use_offsets[i + 1] &&
                                    input->definitions[result.intervals.values[i]] != result.intervals.starts[i] && input->block_starts[first] == result.intervals.starts[i] &&
                                    input->block_starts[last + 1] == result.intervals.ends[i];
                bool operand = false;
                for (uint32_t o = 0; shaped && o < input->phi_operand_offsets[input->phi_count]; o += 1)
                {
                    operand = operand || input->phi_operands[o] == result.intervals.values[i];
                }
                if (shaped && !operand)
                {
                    result.unread_entries[i] = 1;
                    const bool rejected = tier1_cfg_test_mutant(tally, input, &result, output, scratch, &tally->carried_refused);
                    result.unread_entries[i] = 0;
                    tally->carried_mutants += 1;
                    tally->carried_killed += rejected;
                    tried += 1;
                }
            }
            // Order: a move run before one that reads what it writes, where that location is written once on the
            // edge or at the split (not the scratch register or a temporary slot): the reader then finds the other
            // value. The edges' groups of moves, then the splits'.
            tried = 0;
            for (uint32_t g = 0; tried < 8 && g < input->edge_count + result.split_count; g += 1)
            {
                const uint32_t low = g < input->edge_count ? result.edge_move_offsets[g] : result.split_move_offsets[g - input->edge_count];
                const uint32_t high = g < input->edge_count ? result.edge_move_offsets[g + 1] : result.split_move_offsets[g - input->edge_count + 1];
                for (uint32_t m = low; tried < 8 && m < high; m += 1)
                {
                    for (uint32_t n = m + 1; tried < 8 && n < high; n += 1)
                    {
                        const uint32_t location = result.move_destinations[n];
                        const uint32_t class = input->classes[result.move_values[n]];
                        const bool once = location != class * 32 + input->scratch_registers[class] && location < BUSTER_TIER1_CFG_SPILL + allocation->spill_count;
                        if (once && result.move_sources[m] == location && result.move_values[m] != result.move_values[n])
                        {
                            const uint32_t source = result.move_sources[m];
                            const uint32_t destination = result.move_destinations[m];
                            const uint32_t value = result.move_values[m];
                            result.move_sources[m] = result.move_sources[n];
                            result.move_destinations[m] = result.move_destinations[n];
                            result.move_values[m] = result.move_values[n];
                            result.move_sources[n] = source;
                            result.move_destinations[n] = destination;
                            result.move_values[n] = value;
                            const bool rejected = tier1_cfg_test_mutant(tally, input, &result, output, scratch, &tally->order_refused);
                            result.move_sources[n] = result.move_sources[m];
                            result.move_destinations[n] = result.move_destinations[m];
                            result.move_values[n] = result.move_values[m];
                            result.move_sources[m] = source;
                            result.move_destinations[m] = destination;
                            result.move_values[m] = value;
                            tally->order_mutants += 1;
                            tally->order_killed += rejected;
                            tried += 1;
                        }
                    }
                }
            }
            // Definition: a value whose definition needs a register (its first use is at its definition, required)
            // and that has a slot elsewhere, written into its slot instead.
            tried = 0;
            for (uint32_t i = 0; tried < 8 && i < result.intervals.count; i += 1)
            {
                const uint32_t v = result.intervals.values[i];
                const uint32_t first = allocation->fragment_offsets[i];
                const uint32_t use = input->use_offsets[v];
                const bool required = use < input->use_offsets[v + 1] && input->use_positions[use] == input->definitions[v] && input->use_required[use] != 0;
                if (required && result.intervals.starts[i] == input->definitions[v] && allocation->kinds[first] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->spill_ids[v] < allocation->spill_count)
                {
                    // A well-formed slot fragment (no register), so that the definition, not the shape, refuses it.
                    const uint8_t reg = allocation->registers[first];
                    allocation->kinds[first] = BUSTER_TIER1_ALLOCATE_SPILL;
                    allocation->registers[first] = UINT8_MAX;
                    const bool rejected = tier1_cfg_test_mutant(tally, input, &result, output, scratch, &tally->definition_refused);
                    allocation->kinds[first] = BUSTER_TIER1_ALLOCATE_REGISTER;
                    allocation->registers[first] = reg;
                    tally->definition_mutants += 1;
                    tally->definition_killed += rejected;
                    tried += 1;
                }
            }
            // Destination: a split's move to a register sent to another of its class's registers, not the scratch.
            tried = 0;
            for (uint32_t m = result.edge_move_offsets[input->edge_count]; tried < 8 && m < result.move_count; m += 1)
            {
                const uint32_t destination = result.move_destinations[m];
                const uint32_t class = input->classes[result.move_values[m]];
                const uint32_t others = destination < BUSTER_TIER1_CFG_SPILL && destination / 32 == class && destination % 32 != input->scratch_registers[class] ? input->registers[class] & ~(UINT32_C(1) << (destination % 32)) : 0;
                if (others != 0)
                {
                    result.move_destinations[m] = class * 32 + (uint32_t)__builtin_ctz(others);
                    const bool rejected = tier1_cfg_test_mutant(tally, input, &result, output, scratch, &tally->destination_refused);
                    result.move_destinations[m] = destination;
                    tally->destination_mutants += 1;
                    tally->destination_killed += rejected;
                    tried += 1;
                }
            }
            // k0: a mask's register fragment moved to the class's k0.
            tried = 0;
            for (uint32_t f = 0; tried < 8 && f < allocation->fragment_count; f += 1)
            {
                if (allocation->kinds[f] == BUSTER_TIER1_ALLOCATE_REGISTER && allocation->registers[f] != 0)
                {
                    uint32_t interval = 0;
                    while (allocation->fragment_offsets[interval + 1] <= f)
                    {
                        interval += 1;
                    }
                    if (result.intervals.classes[interval] == BUSTER_TIER1_ALLOCATE_MASK)
                    {
                        const uint8_t kept = allocation->registers[f];
                        allocation->registers[f] = 0;
                        const bool rejected = tier1_cfg_test_mutant(tally, input, &result, output, scratch, &tally->k0_refused);
                        allocation->registers[f] = kept;
                        tally->k0_mutants += 1;
                        tally->k0_killed += rejected;
                        tried += 1;
                    }
                }
            }
            // Shape: each column's bound forged past it, read by nothing once refused.
            const uint32_t intervals = result.intervals.count;
            if (input->value_count >= 2 && result.value_interval_offsets[1] < intervals)
            {
                const uint32_t kept = result.value_interval_offsets[1];
                result.value_interval_offsets[1] = intervals + 1;
                tier1_cfg_test_shape(tally, input, &result, output, scratch);
                result.value_interval_offsets[1] = kept;
            }
            if (intervals >= 2)
            {
                const uint32_t kept = allocation->fragment_offsets[1];
                allocation->fragment_offsets[1] = allocation->fragment_count + 1;
                tier1_cfg_test_shape(tally, input, &result, output, scratch);
                allocation->fragment_offsets[1] = kept;
            }
            // A spill fragment where the allocation has no slots, its columns absent; and a register left on a
            // slot's or a recomputation's fragment. The first fragment of each kind.
            bool spilled = false;
            bool stale = false;
            for (uint32_t f = 0; (!spilled || !stale) && f < allocation->fragment_count; f += 1)
            {
                const uint8_t kind = allocation->kinds[f];
                const uint8_t reg = allocation->registers[f];
                const Tier1Allocation kept = *allocation;
                if (!spilled && kind == BUSTER_TIER1_ALLOCATE_REGISTER)
                {
                    allocation->kinds[f] = BUSTER_TIER1_ALLOCATE_SPILL;
                    allocation->registers[f] = UINT8_MAX;
                    allocation->spill_count = 0;
                    allocation->spill_ids = 0;
                    allocation->spill_values = 0;
                    spilled = true;
                }
                else if (!stale && kind != BUSTER_TIER1_ALLOCATE_REGISTER)
                {
                    allocation->registers[f] = 0;
                    stale = true;
                }
                if (allocation->kinds[f] != kind || allocation->registers[f] != reg)
                {
                    tier1_cfg_test_shape(tally, input, &result, output, scratch);
                    *allocation = kept;
                    allocation->kinds[f] = kind;
                    allocation->registers[f] = reg;
                }
            }
            // Columns: slots counted without their columns and past the values, a fragment column, the edges'
            // placements and a move column absent, and fragments that no interval owns, after the last and
            // before the first.
            const Tier1CfgAllocation whole = result;
            result.allocation.spill_count = whole.allocation.spill_count != 0 ? whole.allocation.spill_count : 1;
            result.allocation.spill_ids = 0;
            result.allocation.spill_values = 0;
            tier1_cfg_test_shape(tally, input, &result, output, scratch);
            result = whole;
            result.allocation.spill_count = input->value_count + 1;
            tier1_cfg_test_shape(tally, input, &result, output, scratch);
            result = whole;
            result.allocation.starts = 0;
            tier1_cfg_test_shape(tally, input, &result, output, scratch);
            result = whole;
            result.edge_placements = 0;
            tier1_cfg_test_shape(tally, input, &result, output, scratch);
            result = whole;
            if (result.move_count != 0)
            {
                result.move_values = 0;
                tier1_cfg_test_shape(tally, input, &result, output, scratch);
                result = whole;
            }
            result.allocation.fragment_count += 1;
            tier1_cfg_test_shape(tally, input, &result, output, scratch);
            result = whole;
            allocation->fragment_offsets[0] = 1;
            tier1_cfg_test_shape(tally, input, &result, output, scratch);
            allocation->fragment_offsets[0] = 0;
            Tier1CfgCheck again;
            tier1_cfg_test_expect(tally, tier1_cfg_verify(input, &result, scratch, &again), "the checker accepts the restored allocation");
            tier1_cfg_test_expect(tally, tier1_cfg_test_certify(tally, input, &result, output, scratch, false), "the certificate checker proves the restored allocation");
        }
    }
    else
    {
        tally->constraints += 1;
    }
    arena_pop_to(output, output_saved);
    // A fixture always allocates.
    tier1_cfg_test_expect(tally, !must_allocate || error == TIER1_CFG_OK, name);
    for (uint32_t bind = 0; bind < 3; bind += 1)
    {
        tier1_cfg_test_two_problems(tally, input, output, scratch, bind);
    }
}

// A random value's class: a half general, a quarter vector and a quarter opmask.
static uint8_t tier1_cfg_test_class(uint64_t* random)
{
    const uint64_t draw = tier1_cfg_test_random(random) % 8;
    return draw < 2 ? BUSTER_TIER1_ALLOCATE_VECTOR : draw < 4 ? BUSTER_TIER1_ALLOCATE_MASK : BUSTER_TIER1_ALLOCATE_GPR;
}

// A random SSA program over a random CFG: a chain through every block, so that all are reachable, and extra edges
// forward, backward and to itself, none into the entry. Values are used in their own block after their definition
// or in blocks their definition's block dominates; phis at joins take a same-class value available at the end of
// each predecessor. The opmask class has one to seven of k1 to k7 and k0 its scratch, and some of its fixed ranges
// clobber every mask, as a call does.
static bool tier1_cfg_test_generate(Tier1CfgTestBuilder* builder, uint64_t seed, Arena* arena, Arena* scratch)
{
    uint64_t random = seed * 2 + 1;
    memset(builder, 0, sizeof(*builder));
    builder->block_count = 1 + (uint32_t)(tier1_cfg_test_random(&random) % 20);
    const uint32_t blocks = builder->block_count;
    for (uint32_t b = 0; b < blocks; b += 1)
    {
        builder->instructions[b] = 1 + (uint32_t)(tier1_cfg_test_random(&random) % 5);
    }
    for (uint32_t b = 0; b + 1 < blocks; b += 1)
    {
        tier1_cfg_test_edge(builder, b, b + 1);
    }
    for (uint32_t b = 0; b < blocks && blocks > 1; b += 1)
    {
        const uint32_t extra = (uint32_t)(tier1_cfg_test_random(&random) % 3);
        for (uint32_t x = 0; x < extra && builder->edge_count < BUSTER_TIER1_CFG_TEST_EDGES; x += 1)
        {
            const uint32_t target = 1 + (uint32_t)(tier1_cfg_test_random(&random) % (blocks - 1));
            bool duplicate = false;
            for (uint32_t e = 0; e < builder->edge_count; e += 1)
            {
                duplicate = duplicate || (builder->edge_from[e] == b && builder->edge_to[e] == target);
            }
            if (!duplicate)
            {
                tier1_cfg_test_edge(builder, b, target);
            }
        }
    }
    const uint32_t gpr_size = 1 + (uint32_t)(tier1_cfg_test_random(&random) % 8);
    const uint32_t vector_size = 1 + (uint32_t)(tier1_cfg_test_random(&random) % 6);
    builder->scratch[0] = 11;
    builder->scratch[1] = 15;
    for (uint32_t r = 0, n = 0; r < 16 && n < gpr_size; r += 1)
    {
        if (r != 4 && r != 11)
        {
            builder->registers[0] |= 1u << r;
            n += 1;
        }
    }
    for (uint32_t r = 0; r < vector_size; r += 1)
    {
        builder->registers[1] |= 1u << r;
    }
    const uint32_t mask_size = 1 + (uint32_t)(tier1_cfg_test_random(&random) % 7);
    builder->scratch[BUSTER_TIER1_ALLOCATE_MASK] = 0;
    for (uint32_t r = 1; r <= mask_size; r += 1)
    {
        builder->registers[BUSTER_TIER1_ALLOCATE_MASK] |= 1u << r;
    }
    // Dominance, for available values.
    uint32_t successor_offsets[BUSTER_TIER1_CFG_TEST_BLOCKS + 1];
    uint32_t successors[BUSTER_TIER1_CFG_TEST_EDGES];
    uint32_t predecessor_offsets[BUSTER_TIER1_CFG_TEST_BLOCKS + 1];
    uint32_t predecessors[BUSTER_TIER1_CFG_TEST_EDGES];
    uint32_t edge = 0;
    uint32_t predecessor = 0;
    for (uint32_t b = 0; b < blocks; b += 1)
    {
        successor_offsets[b] = edge;
        predecessor_offsets[b] = predecessor;
        for (uint32_t e = 0; e < builder->edge_count; e += 1)
        {
            if (builder->edge_from[e] == b)
            {
                successors[edge++] = builder->edge_to[e];
            }
            if (builder->edge_to[e] == b)
            {
                predecessors[predecessor++] = builder->edge_from[e];
            }
        }
    }
    successor_offsets[blocks] = edge;
    predecessor_offsets[blocks] = predecessor;
    const DominatorCfg cfg = {
        .block_count = blocks,
        .edge_count = builder->edge_count,
        .entry = 0,
        .successor_offsets = successor_offsets,
        .successors = successors,
        .predecessor_offsets = predecessor_offsets,
        .predecessors = predecessors,
    };
    DominatorOptions options = dominators_options();
    options.outputs = 0;
    Dominators dominance;
    bool valid = dominators_analyze(arena, scratch, &cfg, options, &dominance) == DOMINATOR_OK;
    // Parameters at the entry's first instruction: two general values, a vector one and a mask.
    for (uint32_t p = 0; valid && p < 4; p += 1)
    {
        const uint8_t class = p < 2 ? BUSTER_TIER1_ALLOCATE_GPR : p == 2 ? BUSTER_TIER1_ALLOCATE_VECTOR : BUSTER_TIER1_ALLOCATE_MASK;
        tier1_cfg_test_value(builder, 0, 0, class, builder->registers[class]);
    }
    // Phis at joins, in block order, before the instructions: their operands come from predecessors' ends, which
    // the instructions below fill; so pick operands afterwards.
    uint32_t phi_blocks[BUSTER_TIER1_CFG_TEST_PHIS];
    for (uint32_t b = 1; valid && b < blocks; b += 1)
    {
        const uint32_t count = predecessor_offsets[b + 1] - predecessor_offsets[b];
        const uint32_t phis = count >= 2 ? (uint32_t)(tier1_cfg_test_random(&random) % 4) : 0;
        for (uint32_t k = 0; k < phis && builder->phi_count < 32; k += 1)
        {
            const uint8_t class = tier1_cfg_test_class(&random);
            const uint32_t v = tier1_cfg_test_value(builder, b, UINT32_MAX, class, builder->registers[class]);
            builder->phi_value[builder->phi_count] = v;
            phi_blocks[builder->phi_count] = b;
            builder->phi_count += 1;
        }
    }
    // Instructions: uses of available values, then a definition.
    for (uint32_t b = 0; valid && b < blocks; b += 1)
    {
        for (uint32_t i = 0; i < builder->instructions[b]; i += 1)
        {
            if (b != 0 || i != 0)
            {
                const uint32_t uses = (uint32_t)(tier1_cfg_test_random(&random) % 3);
                for (uint32_t x = 0; x < uses && builder->use_count < BUSTER_TIER1_CFG_TEST_USES; x += 1)
                {
                    // A random value, until one is available here.
                    bool found = false;
                    for (uint32_t attempt = 0; !found && attempt < 12; attempt += 1)
                    {
                        const uint32_t v = (uint32_t)(tier1_cfg_test_random(&random) % builder->value_count);
                        const uint32_t home = builder->def_block[v];
                        const bool here = home == b && (builder->def_instruction[v] == UINT32_MAX || builder->def_instruction[v] < i);
                        const bool above = home != b && dominators_dominates(&dominance, home, b);
                        found = here || above;
                        if (found)
                        {
                            tier1_cfg_test_use(builder, v, b, i, tier1_cfg_test_random(&random) % 3 == 0);
                        }
                    }
                }
            }
            if (b != 0 || i != 0)
            {
                if (tier1_cfg_test_random(&random) % 5 != 0 && builder->value_count < BUSTER_TIER1_CFG_TEST_VALUES - 1)
                {
                    const uint8_t class = tier1_cfg_test_class(&random);
                    uint32_t mask = builder->registers[class];
                    if (tier1_cfg_test_random(&random) % 8 == 0)
                    {
                        // A narrowed mask: the lowest allowed register alone.
                        mask &= (uint32_t)-(int32_t)mask;
                    }
                    const uint32_t v = tier1_cfg_test_value(builder, b, i, class, mask);
                    builder->remat[v] = tier1_cfg_test_random(&random) % 10 == 0;
                }
            }
            const bool defined = (b != 0 || i != 0) && builder->value_count != 0 && builder->def_block[builder->value_count - 1] == b && builder->def_instruction[builder->value_count - 1] == i;
            const uint32_t fixed_before = builder->fixed_count;
            if (tier1_cfg_test_random(&random) % 9 == 0 && builder->fixed_count < BUSTER_TIER1_CFG_TEST_FIXED)
            {
                const uint64_t draw = tier1_cfg_test_random(&random) % 6;
                const uint8_t class = draw < 2 ? BUSTER_TIER1_ALLOCATE_VECTOR : draw == 2 ? BUSTER_TIER1_ALLOCATE_MASK : BUSTER_TIER1_ALLOCATE_GPR;
                uint32_t pick = builder->registers[class];
                for (uint32_t skip = (uint32_t)(tier1_cfg_test_random(&random) % 4); skip > 0 && (pick & (pick - 1)) != 0; skip -= 1)
                {
                    pick &= pick - 1;
                }
                pick &= (uint32_t)-(int32_t)pick;
                // Half the opmask class's: a call's, which clobbers every mask.
                pick = class == BUSTER_TIER1_ALLOCATE_MASK && tier1_cfg_test_random(&random) % 2 == 0 ? builder->registers[class] : pick;
                builder->fixed_block[builder->fixed_count] = b;
                builder->fixed_instruction[builder->fixed_count] = i;
                builder->fixed_class[builder->fixed_count] = class;
                builder->fixed_register[builder->fixed_count] = pick;
                builder->fixed_count += pick != 0;
            }
            // Half the definitions where no fixed range clobbers need a register, as an instruction's destination does.
            if (defined && builder->fixed_count == fixed_before && builder->use_count < BUSTER_TIER1_CFG_TEST_USES && tier1_cfg_test_random(&random) % 2 == 0)
            {
                tier1_cfg_test_definition_use(builder, builder->value_count - 1);
            }
        }
    }
    // Phi operands: a same-class value available at the end of each predecessor.
    for (uint32_t k = 0; valid && k < builder->phi_count; k += 1)
    {
        const uint32_t b = phi_blocks[k];
        const uint8_t class = builder->classes[builder->phi_value[k]];
        for (uint32_t j = 0; j < predecessor_offsets[b + 1] - predecessor_offsets[b]; j += 1)
        {
            const uint32_t p = predecessors[predecessor_offsets[b] + j];
            // A parameter of the class, or a random value available there.
            uint32_t chosen = class == BUSTER_TIER1_ALLOCATE_GPR ? 0 : class == BUSTER_TIER1_ALLOCATE_VECTOR ? 2 : 3;
            bool found = false;
            for (uint32_t attempt = 0; !found && attempt < 16; attempt += 1)
            {
                const uint32_t v = (uint32_t)(tier1_cfg_test_random(&random) % builder->value_count);
                const uint32_t home = builder->def_block[v];
                found = builder->classes[v] == class && (home == p || dominators_dominates(&dominance, home, p));
                chosen = found ? v : chosen;
            }
            // One in six undefined: the IR's undef, which the edge moves nothing for (#141).
            builder->phi_operand[k][j] = tier1_cfg_test_random(&random) % 6 == 0 ? BUSTER_TIER1_CFG_NONE : chosen;
        }
    }
    // Three cases in four let a phi web share slots (#1216), one value in seven at another slot width, which keeps it
    // apart; drawn from the seed, not the generator, so the programs stay those of earlier runs.
    builder->share_slots = seed % 4 != 3;
    for (uint32_t v = 0; v < builder->value_count; v += 1)
    {
        builder->slot_width[v] = v % 7 == 6 ? 4 : 8;
    }
    // Half the cases have fixed registers' sequences (#2767), from a generator of their own, so that the programs stay
    // those of earlier runs: at one instruction in six, of one to three general registers, a third of them with a reader,
    // a third with an operand and a third with a result that avoid them (any general value: what avoids is the input's).
    uint64_t sequenced = seed ^ UINT64_C(0x2767276727672767);
    builder->sequence_count = 0;
    for (uint32_t b = 0; valid && seed % 2 == 0 && b < builder->block_count; b += 1)
    {
        for (uint32_t i = 0; i < builder->instructions[b] && builder->sequence_count < BUSTER_TIER1_CFG_TEST_SEQUENCES; i += 1)
        {
            // The class's lowest register after skipping up to two, and half the time another one drawn at random.
            const uint32_t registers = builder->registers[BUSTER_TIER1_ALLOCATE_GPR];
            uint32_t pick = registers;
            for (uint32_t skip = (uint32_t)(tier1_cfg_test_random(&sequenced) % 3); skip > 0 && (pick & (pick - 1)) != 0; skip -= 1)
            {
                pick &= pick - 1;
            }
            pick &= (uint32_t)-(int32_t)pick;
            const uint32_t other = (uint32_t)(tier1_cfg_test_random(&sequenced) % 16);
            pick |= tier1_cfg_test_random(&sequenced) % 2 == 0 && ((registers >> other) & 1) != 0 ? 1u << other : 0;
            const uint32_t reader = (uint32_t)(tier1_cfg_test_random(&sequenced) % builder->value_count);
            builder->sequence_block[builder->sequence_count] = b;
            builder->sequence_instruction[builder->sequence_count] = i;
            builder->sequence_registers[builder->sequence_count] = pick;
            builder->sequence_reader[builder->sequence_count] = tier1_cfg_test_random(&sequenced) % 3 == 0 && builder->classes[reader] == BUSTER_TIER1_ALLOCATE_GPR ? reader : BUSTER_TIER1_CFG_NONE;
            const uint32_t operand = (uint32_t)(tier1_cfg_test_random(&sequenced) % builder->value_count);
            const uint32_t result = (uint32_t)(tier1_cfg_test_random(&sequenced) % builder->value_count);
            builder->sequence_operand[builder->sequence_count] = tier1_cfg_test_random(&sequenced) % 3 == 0 && builder->classes[operand] == BUSTER_TIER1_ALLOCATE_GPR ? operand : BUSTER_TIER1_CFG_NONE;
            builder->sequence_result[builder->sequence_count] = tier1_cfg_test_random(&sequenced) % 3 == 0 && builder->classes[result] == BUSTER_TIER1_ALLOCATE_GPR ? result : BUSTER_TIER1_CFG_NONE;
            builder->sequence_count += tier1_cfg_test_random(&sequenced) % 6 == 0 && pick != 0;
        }
    }
    return valid;
}

// A loop's phi whose operand from the entry is undefined (#141): a local a block assigns before reading it, carried
// around the loop. Both paths allocate it, the checker accepts it, and the entry's edge moves nothing for the phi; with
// every operand undefined, no edge moves anything.
static void tier1_cfg_test_undefined(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builder;
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        Tier1CfgTestBuilder* loop = &builder;
        memset(loop, 0, sizeof(*loop));
        loop->block_count = 4;
        loop->instructions[0] = 1;
        loop->instructions[1] = 1;
        loop->instructions[2] = 2;
        loop->instructions[3] = 1;
        tier1_cfg_test_edge(loop, 0, 1);
        tier1_cfg_test_edge(loop, 2, 1);
        tier1_cfg_test_edge(loop, 1, 2);
        tier1_cfg_test_edge(loop, 2, 3);
        loop->registers[0] = 0x000f;
        loop->registers[1] = 0x000f;
        loop->scratch[0] = 11;
        loop->scratch[1] = 15;
        const uint32_t next = tier1_cfg_test_value(loop, 2, 0, 0, 0x000f);
        const uint32_t operands[] = {
            BUSTER_TIER1_CFG_NONE,
            pass == 0 ? next : BUSTER_TIER1_CFG_NONE,
        };
        const uint32_t x = tier1_cfg_test_phi(loop, 1, 0, operands);
        tier1_cfg_test_use(loop, x, 2, 0, true);
        tier1_cfg_test_use(loop, x, 3, 0, true);
        tier1_cfg_test_definition_use(loop, next);
        const uint64_t saved = output->length;
        Tier1CfgInput input;
        const bool built = tier1_cfg_test_build(loop, output, &input);
        tier1_cfg_test_expect(tally, built, "the undefined operands' input");
        if (built)
        {
            const uint64_t mark = output->length;
            Tier1CfgAllocation result = { 0 };
            const Tier1CfgError error = tier1_cfg_allocate(&input, output, scratch, &result);
            Tier1CfgCheck check;
            tier1_cfg_test_expect(tally, error == TIER1_CFG_OK && tier1_cfg_verify(&input, &result, scratch, &check), "a phi of undefined operands allocates and checks");
            // The entry's edge is the first: block 0's only successor.
            const uint32_t entry_moves = error == TIER1_CFG_OK ? result.edge_move_offsets[1] - result.edge_move_offsets[0] : 1;
            tier1_cfg_test_expect(tally, entry_moves == 0, "the entry's edge moves nothing for an undefined operand");
            tier1_cfg_test_expect(tally, error != TIER1_CFG_OK || pass == 0 || result.move_count == 0, "a phi of undefined operands alone moves nothing");
            arena_pop_to(output, mark);
        }
        tier1_cfg_test_case(tally, &input, output, scratch, "undefined phi operands", true, 0, 0, true);
        arena_pop_to(output, saved);
    }
}

// The moves of edge `from` -> `to` in an allocation of `input`, or UINT32_MAX where there is no such edge.
static uint32_t tier1_cfg_test_edge_moves(const Tier1CfgInput* input, const Tier1CfgAllocation* result, uint32_t from, uint32_t to)
{
    uint32_t moves = UINT32_MAX;
    for (uint32_t e = input->successor_offsets[from]; e < input->successor_offsets[from + 1]; e += 1)
    {
        moves = input->successors[e] == to ? result->edge_move_offsets[e + 1] - result->edge_move_offsets[e] : moves;
    }
    return moves;
}

static void tier1_cfg_test_ordinals(Tier1CfgTestTally* tally, const Tier1CfgInput* input, Arena* scratch)
{
    const uint64_t saved = scratch->length;
    uint32_t* ordinals = 0;
    const bool built = tier1_cfg_input_valid(input, scratch) && tier1_cfg_edge_ordinals(input, scratch, &ordinals);
    tier1_cfg_test_expect(tally, built, "successor edges map to their original predecessor ordinals");
    const uint64_t retained = scratch->length;
    if (built)
    {
        bool matched = true;
        for (uint32_t b = 0; matched && b < input->block_count; b += 1)
        {
            for (uint32_t e = input->successor_offsets[b]; matched && e < input->successor_offsets[b + 1]; e += 1)
            {
                const uint32_t target = input->successors[e];
                const uint32_t first = input->predecessor_offsets[target];
                matched = ordinals[e] < input->predecessor_offsets[target + 1] - first && input->predecessors[first + ordinals[e]] == b;
            }
        }
        tier1_cfg_test_expect(tally, matched, "the ordinal mapping preserves unsorted rows and self edges");
        tier1_cfg_test_expect(tally, retained - saved <= ((uint64_t)input->edge_count + 1) * sizeof(uint32_t) + _Alignof(uint32_t) - 1, "only edge ordinals remain in scratch after mapping");
    }
    arena_pop_to(scratch, saved);
    const uint64_t capacities[] = {
        saved,
        retained,
        retained + ((uint64_t)input->block_count + 1) * sizeof(uint32_t),
    };
    for (uint32_t stage = 0; built && stage < BUSTER_ARRAY_LENGTH(capacities); stage += 1)
    {
        uint32_t sentinel = 1384;
        uint32_t* refused = &sentinel;
        Arena bounded = *scratch;
        bounded.capacity = capacities[stage];
        const bool mapped = tier1_cfg_edge_ordinals(input, &bounded, &refused);
        tier1_cfg_test_expect(tally, !mapped && bounded.length == saved && refused == &sentinel && sentinel == 1384, "edge-map capacity failure restores scratch and leaves its output unchanged");
    }
}

// #1384: a phi use belongs to its predecessor, not its successor. One home value may feed several edges and phis
// without global liveness; a single remote occurrence (ordinary or phi) must still make it global. The operands have
// no ordinary use at their home's tail, including across a fixed clobber, so forgetting the edge use truncates them.
static void tier1_cfg_test_local_phis(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builder;
    for (uint32_t class = 0; class < BUSTER_TIER1_ALLOCATE_CLASSES; class += 1)
    {
        for (uint32_t mode = 0; mode < 7; mode += 1)
        {
            memset(&builder, 0, sizeof(builder));
            builder.block_count = mode == 6 ? 3 : 5;
            for (uint32_t b = 0; b < builder.block_count; b += 1)
            {
                builder.instructions[b] = 5;
            }
            builder.registers[BUSTER_TIER1_ALLOCATE_GPR] = 0xf;
            builder.registers[BUSTER_TIER1_ALLOCATE_VECTOR] = 0xf;
            builder.registers[BUSTER_TIER1_ALLOCATE_MASK] = 0x1e;
            builder.scratch[BUSTER_TIER1_ALLOCATE_GPR] = 11;
            builder.scratch[BUSTER_TIER1_ALLOCATE_VECTOR] = 15;
            builder.scratch[BUSTER_TIER1_ALLOCATE_MASK] = 0;
            const uint32_t registers = builder.registers[class];
            const uint32_t x = tier1_cfg_test_value(&builder, 0, 0, (uint8_t)class, mode == 3 || mode == 4 ? 0 : registers);
            builder.remat[x] = mode == 4;
            uint32_t self = BUSTER_TIER1_CFG_NONE;
            if (mode == 6)
            {
                tier1_cfg_test_edge(&builder, 0, 1);
                tier1_cfg_test_edge(&builder, 1, 1);
                tier1_cfg_test_edge(&builder, 1, 2);
                const uint32_t operands[] = {
                    x,
                    builder.value_count,
                };
                self = tier1_cfg_test_phi(&builder, 1, (uint8_t)class, operands);
                tier1_cfg_test_use(&builder, self, 1, 0, true);
            }
            else
            {
                tier1_cfg_test_edge(&builder, 0, 2);
                tier1_cfg_test_edge(&builder, 0, 1);
                tier1_cfg_test_edge(&builder, 0, 4);
                // The join's predecessor order is deliberately the reverse of layout order.
                tier1_cfg_test_edge(&builder, 2, 3);
                tier1_cfg_test_edge(&builder, 1, 3);
                const uint32_t operands[] = { x };
                const uint32_t left = tier1_cfg_test_phi(&builder, 1, (uint8_t)class, operands);
                const uint32_t right = tier1_cfg_test_phi(&builder, 2, (uint8_t)class, operands);
                const uint32_t repeated = tier1_cfg_test_phi(&builder, 2, (uint8_t)class, operands);
                const uint32_t undefined_operands[] = { BUSTER_TIER1_CFG_NONE };
                const uint32_t undefined = tier1_cfg_test_phi(&builder, 2, (uint8_t)class, undefined_operands);
                const uint32_t joined_operands[] = {
                    right,
                    mode == 1 ? x : left,
                };
                const uint32_t joined = tier1_cfg_test_phi(&builder, 3, (uint8_t)class, joined_operands);
                tier1_cfg_test_use(&builder, repeated, 2, 0, true);
                tier1_cfg_test_use(&builder, undefined, 2, 1, false);
                tier1_cfg_test_use(&builder, joined, 3, 0, true);
                if (mode == 2)
                {
                    tier1_cfg_test_use(&builder, x, 3, 1, true);
                }
                if (mode == 5)
                {
                    tier1_cfg_test_definition_use(&builder, x);
                    builder.fixed_block[0] = 0;
                    builder.fixed_instruction[0] = 2;
                    builder.fixed_class[0] = (uint8_t)class;
                    builder.fixed_register[0] = registers;
                    builder.fixed_count = 1;
                }
            }
            const uint64_t saved = output->length;
            Tier1CfgInput input = { 0 };
            const bool built = tier1_cfg_test_build(&builder, output, &input);
            tier1_cfg_test_expect(tally, built, "edge-local phi fixture builds");
            if (built)
            {
                tier1_cfg_test_ordinals(tally, &input, scratch);
            }
            if (built)
            {
                const uint64_t mark = output->length;
                const uint64_t scratch_mark = scratch->length;
                Tier1CfgAllocation result = { 0 };
                const Tier1CfgError error = tier1_cfg_allocate(&input, output, scratch, &result);
                tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "edge-local phi fixture allocates");
                if (error == TIER1_CFG_OK)
                {
                    const uint32_t global = mode == 1 || mode == 2 ? 1 : 0;
                    const uint32_t local = mode == 1 ? 1 : mode == 2 || mode == 6 ? 2 : 3;
                    tier1_cfg_test_expect(tally, result.cross_block_values == global, "only remote ordinary or phi uses need global liveness");
                    tier1_cfg_test_expect(tally, result.edge_local_values == local, "repeated home phi occurrences count each local operand once");
                    tier1_cfg_test_expect(tally, global != 0 || result.liveness_bytes == 0, "edge-local operands alone allocate no dense liveness matrix");
                    const uint32_t exit = input.block_starts[1] - 1;
                    const uint32_t interval = tier1_cfg_interval_oracle(&result, x, exit);
                    tier1_cfg_test_expect(tally, interval != BUSTER_TIER1_CFG_NONE && result.intervals.ends[interval] >= input.block_starts[1], "a phi operand's home interval reaches the outgoing edge");
                    if (mode == 6)
                    {
                        const uint32_t self_interval = tier1_cfg_interval_oracle(&result, self, input.block_starts[2] - 1);
                        tier1_cfg_test_expect(tally, self_interval != BUSTER_TIER1_CFG_NONE, "a self-phi's home interval reaches its backedge");
                        tier1_cfg_test_expect(tally, tier1_cfg_test_edge_moves(&input, &result, 1, 2) == 0, "a self-phi's unused sibling edge copies nothing");
                    }
                    else
                    {
                        tier1_cfg_test_expect(tally, tier1_cfg_test_edge_moves(&input, &result, 0, 4) == 0, "an edge without a phi use copies no home operand");
                    }
                    Tier1CfgCheck check;
                    tier1_cfg_test_expect(tally, tier1_cfg_verify(&input, &result, scratch, &check), "the independent checker accepts local phi edge moves");
                }
                tier1_cfg_test_expect(tally, scratch->length == scratch_mark, "edge-local allocation restores scratch");
                arena_pop_to(output, mark);
            }
            if (built)
            {
                tier1_cfg_test_case(tally, &input, output, scratch, "edge-local phi operands", true, 0, 0, true);
            }
            arena_pop_to(output, saved);
        }
    }
    // Empty edge columns and a disconnected self edge exercise the map independently of phi allocation.
    const uint32_t starts[] = {
        0,
        1,
        2,
    };
    const uint32_t offsets[] = {
        0,
        0,
        1,
    };
    const uint32_t phis[] = {
        0,
        0,
        0,
    };
    const uint32_t self[] = { 1 };
    Tier1CfgInput input =
    {
        .block_starts = starts,
        .successor_offsets = offsets,
        .successors = self,
        .predecessor_offsets = offsets,
        .predecessors = self,
        .phi_offsets = phis,
        .phi_operand_offsets = phis,
        .block_count = 2,
        .edge_count = 1,
        .position_count = 2,
    };
    tier1_cfg_test_ordinals(tally, &input, scratch);
    input.block_count = 1;
    input.edge_count = 0;
    input.position_count = 1;
    input.successors = 0;
    input.predecessors = 0;
    tier1_cfg_test_ordinals(tally, &input, scratch);
}

// A wide join with one distinct local operand per predecessor, and optionally one value live through all arms.
// The fixed scratch allowance admits preparation proportional to the blocks and actual live memberships. At 8,193
// arms the old four matrices alone exceed 32 MiB; the allowance is 8 MiB. This checks capacity, not elapsed time.
// #2782: a handler that an earlier block's `goto` and a later dispatch's multiway branch both enter (Lua's OP_TFORCALL,
// after OP_TFORPREP) takes the dispatch's convention for its phi: the phi's register where the handler starts is its
// operand's along the branch at the branch's first successor's entry, not the `goto` operand's, which can't share the
// dispatch's value's register. Block 4 branches to blocks 1, 2 and 3; block 2 computes y from pc, which lives on past it,
// and goes to block 3, whose phi joins y and pc, and which branches to block 4 or out to block 5: with one successor, its
// phi would take the dispatch's phi's place by #2734's alternate alone.
static void tier1_cfg_test_dispatch_entry(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builder;
    memset(&builder, 0, sizeof(builder));
    builder.block_count = 6;
    for (uint32_t b = 0; b < builder.block_count; b += 1)
    {
        builder.instructions[b] = 4;
    }
    builder.registers[BUSTER_TIER1_ALLOCATE_GPR] = 0xf;
    builder.registers[BUSTER_TIER1_ALLOCATE_VECTOR] = 0xf;
    builder.registers[BUSTER_TIER1_ALLOCATE_MASK] = 0x1e;
    builder.scratch[BUSTER_TIER1_ALLOCATE_GPR] = 11;
    builder.scratch[BUSTER_TIER1_ALLOCATE_VECTOR] = 15;
    builder.scratch[BUSTER_TIER1_ALLOCATE_MASK] = 0;
    const uint32_t registers = builder.registers[BUSTER_TIER1_ALLOCATE_GPR];
    tier1_cfg_test_edge(&builder, 0, 4);
    tier1_cfg_test_edge(&builder, 1, 4);
    tier1_cfg_test_edge(&builder, 2, 3);
    tier1_cfg_test_edge(&builder, 3, 4);
    tier1_cfg_test_edge(&builder, 3, 5);
    tier1_cfg_test_edge(&builder, 4, 1);
    tier1_cfg_test_edge(&builder, 4, 2);
    tier1_cfg_test_edge(&builder, 4, 3);
    const uint32_t start = tier1_cfg_test_value(&builder, 0, 0, BUSTER_TIER1_ALLOCATE_GPR, registers);
    const uint32_t x = tier1_cfg_test_value(&builder, 1, 1, BUSTER_TIER1_ALLOCATE_GPR, registers);
    const uint32_t y = tier1_cfg_test_value(&builder, 2, 1, BUSTER_TIER1_ALLOCATE_GPR, registers);
    // pc, the dispatch's phi, then the handler's: each takes the other.
    const uint32_t pc_operands[] = {
        start,
        x,
        builder.value_count + 1,
    };
    const uint32_t pc = tier1_cfg_test_phi(&builder, 4, BUSTER_TIER1_ALLOCATE_GPR, pc_operands);
    const uint32_t joined_operands[] = {
        y,
        pc,
    };
    const uint32_t joined = tier1_cfg_test_phi(&builder, 3, BUSTER_TIER1_ALLOCATE_GPR, joined_operands);
    tier1_cfg_test_definition_use(&builder, start);
    tier1_cfg_test_use(&builder, pc, 1, 0, true);
    tier1_cfg_test_definition_use(&builder, x);
    tier1_cfg_test_use(&builder, pc, 2, 0, true);
    tier1_cfg_test_definition_use(&builder, y);
    tier1_cfg_test_use(&builder, pc, 2, 2, true);
    tier1_cfg_test_use(&builder, joined, 3, 0, true);
    const uint64_t saved = output->length;
    Tier1CfgInput input = { 0 };
    const bool built = tier1_cfg_test_build(&builder, output, &input);
    tier1_cfg_test_expect(tally, built, "dispatch-entry fixture builds");
    if (built)
    {
        const uint64_t mark = output->length;
        Tier1CfgAllocation result = { 0 };
        const Tier1CfgError error = tier1_cfg_allocate(&input, output, scratch, &result);
        tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "dispatch-entry fixture allocates");
        if (error == TIER1_CFG_OK)
        {
            uint32_t handler = 0;
            uint32_t convention = 0;
            uint32_t taken = 0;
            uint8_t kinds[3] = { 0 };
            const bool found = tier1_cfg_location(&result, joined, input.block_starts[3], &handler, &kinds[0]) && tier1_cfg_location(&result, pc, input.block_starts[1], &convention, &kinds[1]) &&
                               tier1_cfg_location(&result, y, input.block_starts[3] - 1, &taken, &kinds[2]);
            tier1_cfg_test_expect(tally, found && kinds[0] == BUSTER_TIER1_ALLOCATE_REGISTER && kinds[1] == BUSTER_TIER1_ALLOCATE_REGISTER && taken != convention,
                                  "the dispatch-entry fixture's goto operand can't take the dispatch's register");
            tier1_cfg_test_expect(tally, found && handler == convention, "a handler a goto enters takes the dispatch's convention for its phi");
            Tier1CfgCheck check;
            tier1_cfg_test_expect(tally, tier1_cfg_verify(&input, &result, scratch, &check), "the independent checker accepts the dispatch-entry fixture");
        }
        arena_pop_to(output, mark);
    }
    if (built)
    {
        tier1_cfg_test_case(tally, &input, output, scratch, "dispatch entry", true, 0, 0, true);
    }
    arena_pop_to(output, saved);
}

static void tier1_cfg_test_phi_storage(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    const uint32_t widths[] = {
        8,
        65,
        513,
        8193,
    };
    const uint64_t allowance = 8ull << 20;
    for (uint32_t size = 0; size < BUSTER_ARRAY_LENGTH(widths); size += 1)
    {
        const uint64_t saved = output->length;
        const uint32_t width = widths[size];
        const uint32_t blocks = width + 2;
        const uint32_t edges = width * 2;
        const uint32_t values = width + 2;
        const uint32_t join = width + 1;
        uint32_t* starts = arena_push_array(output, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* successor_offsets = arena_push_array(output, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* successors = arena_push_array(output, edges, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* predecessor_offsets = arena_push_array(output, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* predecessors = arena_push_array(output, edges, sizeof(uint32_t), _Alignof(uint32_t));
        uint8_t* classes = arena_push(output, values, 1);
        uint32_t* allowed = arena_push_array(output, values, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* definitions = arena_push_array(output, values, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* use_offsets = arena_push_array(output, values + 1, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* phi_offsets = arena_push_array(output, blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
        uint32_t* operands = arena_push_array(output, width, sizeof(uint32_t), _Alignof(uint32_t));
        const bool built = starts != 0 && successor_offsets != 0 && successors != 0 && predecessor_offsets != 0 && predecessors != 0 && classes != 0 && allowed != 0 && definitions != 0 && use_offsets != 0 && phi_offsets != 0 && operands != 0;
        tier1_cfg_test_expect(tally, built, "wide phi fixture memory");
        if (built)
        {
            for (uint32_t b = 0; b <= blocks; b += 1)
            {
                starts[b] = b * 4;
                successor_offsets[b] = b == 0 ? 0 : b <= join ? width + b - 1 : edges;
                predecessor_offsets[b] = b <= 1 ? 0 : b <= join ? b - 1 : edges;
                phi_offsets[b] = b <= join ? 0 : 1;
            }
            for (uint32_t j = 0; j < width; j += 1)
            {
                successors[j] = width - j;
                successors[width + j] = join;
                predecessors[j] = 0;
                predecessors[width + j] = width - j;
                operands[j] = width - j;
            }
            for (uint32_t v = 0; v < values; v += 1)
            {
                classes[v] = BUSTER_TIER1_ALLOCATE_GPR;
                allowed[v] = 0xf;
                definitions[v] = starts[v] + (v == join ? 0 : 1);
            }
            const uint32_t phi_values[] = { join };
            const uint32_t phi_operand_offsets[] = { 0, width };
            const uint32_t use_positions[] = { starts[join] + 1, starts[join] + 2 };
            for (uint32_t global = 0; global < 2; global += 1)
            {
                use_offsets[0] = 0;
                for (uint32_t v = 1; v < values; v += 1)
                {
                    use_offsets[v] = global;
                }
                use_offsets[values] = global + 1;
                const Tier1CfgInput input =
                {
                    .block_starts = starts,
                    .successor_offsets = successor_offsets,
                    .successors = successors,
                    .predecessor_offsets = predecessor_offsets,
                    .predecessors = predecessors,
                    .classes = classes,
                    .allowed_registers = allowed,
                    .definitions = definitions,
                    .use_offsets = use_offsets,
                    .use_positions = use_positions + (global == 0 ? 1 : 0),
                    .phi_offsets = phi_offsets,
                    .phi_values = phi_values,
                    .phi_operand_offsets = phi_operand_offsets,
                    .phi_operands = operands,
                    .block_count = blocks,
                    .edge_count = edges,
                    .value_count = values,
                    .use_count = global + 1,
                    .phi_count = 1,
                    .position_count = starts[blocks],
                    .registers = {
                        0xf,
                        0,
                        0,
                    },
                    .scratch_registers = {
                        11,
                        15,
                        0,
                    },
                };
                tier1_cfg_test_ordinals(tally, &input, scratch);
                const uint64_t mark = output->length;
                Arena bounded = *scratch;
                bounded.capacity = bounded.length + allowance < bounded.capacity ? bounded.length + allowance : bounded.capacity;
                Tier1CfgPrepared prepared = { 0 };
                const Tier1CfgError error = tier1_cfg_prepare(&input, output, &bounded, &prepared);
                tier1_cfg_test_expect(tally, error == TIER1_CFG_OK, "wide local-phi preparation fits bounded scratch");
                if (error == TIER1_CFG_OK)
                {
                    const Tier1CfgAllocation* result = &prepared.candidate;
                    tier1_cfg_test_expect(tally, result->cross_block_values == global, "wide phi operands do not widen global liveness");
                    tier1_cfg_test_expect(tally, result->edge_local_values == width, "wide phi operands count once per home");
                    tier1_cfg_test_expect(tally, result->liveness_bytes <= (uint64_t)blocks * 256 && (global != 0 || result->liveness_bytes == 0), "wide phi matrix storage grows only with blocks");
                    bool tails = true;
                    for (uint32_t v = 1; tails && v <= width; v += 1)
                    {
                        const uint32_t first = result->value_interval_offsets[v];
                        const uint32_t end = result->value_interval_offsets[v + 1];
                        tails = end == first + 1 && result->intervals.starts[first] == definitions[v] && result->intervals.ends[first] == starts[v + 1];
                    }
                    tier1_cfg_test_expect(tally, tails, "every wide phi operand has exactly its home-tail interval");
                    tier1_cfg_release(&prepared, &bounded);
                }
                tier1_cfg_test_expect(tally, bounded.length == scratch->length, "bounded preparation restores scratch");
                arena_pop_to(output, mark);
                if (width == widths[0])
                {
                    tier1_cfg_test_case(tally, &input, output, scratch, "wide phi operand semantics", true, 0, 0, true);
                }
            }
        }
        arena_pop_to(output, saved);
    }
}

// #2553's fixtures, each allocated by both paths and checked by the checker, then as a case (tier1_cfg_test_case):
// - A join's phi takes its operand's register at the exit of its immediate dominator, a predecessor: 0 -> 1, 0 -> 2,
//   1 -> 2, c and t defined in 0, z = phi(t, r) at 2, r defined in 1 while u holds t's former register. The critical edge
//   0 -> 2 then moves nothing, where z took r's register, its nearest predecessor's in layout, and the edge moved t.
// - A phi web carried around a loop whose latch's join is laid out before its header keeps one slot: no general registers,
//   0 -> 3, 3 -> 1, 3 -> 2, 1 -> 2, 2 -> 3, 3 -> 4; p2 = phi(q, p1) at 2, p1 = phi(e, p2) at 3, q defined in 1 while p1 is
//   live. Joining p2's operands in order put q in its slot first, which then kept out p1, live at q's definition; the
//   phi-to-phi pairs join first, so that the loop's edges 2 -> 3 and 3 -> 2 move nothing.
// And tier1_cfg_anchors on those two CFGs and a third, whose dispatch handlers precede their join: the first join's anchor
// and chosen predecessor are its dominator, the web's header's and the dispatch join's their latch laid out before them,
// the handlers have none, and neither has the entry.
static void tier1_cfg_test_anchors(Tier1CfgTestTally* tally, const Tier1CfgInput* input, Arena* output, Arena* scratch, const uint32_t* anchors_expected, const uint32_t* chosen_expected, const char* name)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    uint32_t* anchors = arena_push_array(scratch, (uint64_t)input->block_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* chosen = arena_push_array(scratch, (uint64_t)input->block_count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    bool same = anchors != 0 && chosen != 0 && tier1_cfg_anchors(input, output, scratch, anchors, chosen) && output->length == output_mark;
    for (uint32_t b = 0; same && b < input->block_count; b += 1)
    {
        // The chosen predecessor as a block, to compare it with the expected one.
        const uint32_t predecessor = chosen[b] != BUSTER_TIER1_CFG_NONE ? input->predecessors[input->predecessor_offsets[b] + chosen[b]] : BUSTER_TIER1_CFG_NONE;
        same = anchors[b] == anchors_expected[b] && predecessor == chosen_expected[b];
    }
    tier1_cfg_test_expect(tally, same, name);
    arena_pop_to(scratch, scratch_mark);
}

// #2611: tier1_cfg_anchors reads a given dominator tree as it reads its own analysis. The input's CFG's tree, given with no
// shift, and that tree given to the CFG with an entry added before it (shift 1, as the CFG consumer's machine CFG is the
// IR's with an added entry), each give the anchors and chosen predecessors the analysis of that CFG gives.
static void tier1_cfg_test_given_tree(Tier1CfgTestTally* tally, const Tier1CfgInput* input, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t blocks = input->block_count;
    const uint32_t edges = input->edge_count;
    const DominatorCfg graph = {
        .block_count = blocks,
        .edge_count = edges,
        .entry = 0,
        .successor_offsets = input->successor_offsets,
        .successors = input->successors,
        .predecessor_offsets = input->predecessor_offsets,
        .predecessors = input->predecessors,
    };
    DominatorOptions options = dominators_options();
    options.outputs = 0;
    Dominators tree = { 0 };
    bool same = dominators_analyze(output, scratch, &graph, options, &tree) == DOMINATOR_OK;
    // The CFG with an added entry, block 0, whose one successor is the input's entry.
    uint32_t* successor_offsets = arena_push_array(output, (uint64_t)blocks + 2, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* successors = arena_push_array(output, (uint64_t)edges + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* predecessor_offsets = arena_push_array(output, (uint64_t)blocks + 2, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* predecessors = arena_push_array(output, (uint64_t)edges + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* anchors = arena_push_array(output, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* chosen = arena_push_array(output, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* given_anchors = arena_push_array(output, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    uint32_t* given_chosen = arena_push_array(output, (uint64_t)blocks + 1, sizeof(uint32_t), _Alignof(uint32_t));
    same = same && successor_offsets != 0 && successors != 0 && predecessor_offsets != 0 && predecessors != 0 && anchors != 0 && chosen != 0 && given_anchors != 0 && given_chosen != 0;
    if (same)
    {
        successor_offsets[0] = 0;
        successors[0] = 1;
        predecessor_offsets[0] = 0;
        predecessor_offsets[1] = 0;
        predecessors[0] = 0;
        for (uint32_t b = 0; b < blocks; b += 1)
        {
            successor_offsets[b + 1] = input->successor_offsets[b] + 1;
            predecessor_offsets[b + 2] = input->predecessor_offsets[b + 1] + 1;
        }
        successor_offsets[blocks + 1] = edges + 1;
        for (uint32_t e = 0; e < edges; e += 1)
        {
            successors[e + 1] = input->successors[e] + 1;
            predecessors[e + 1] = input->predecessors[e] + 1;
        }
    }
    for (uint32_t shift = 0; same && shift < 2; shift += 1)
    {
        Tier1CfgInput plain = *input;
        plain.dominators = 0;
        plain.dominator_shift = 0;
        if (shift != 0)
        {
            plain.block_count = blocks + 1;
            plain.edge_count = edges + 1;
            plain.successor_offsets = successor_offsets;
            plain.successors = successors;
            plain.predecessor_offsets = predecessor_offsets;
            plain.predecessors = predecessors;
        }
        Tier1CfgInput given = plain;
        given.dominators = &tree;
        given.dominator_shift = shift;
        same = tier1_cfg_anchors(&plain, output, scratch, anchors, chosen) && tier1_cfg_anchors(&given, output, scratch, given_anchors, given_chosen);
        for (uint32_t b = 0; same && b < plain.block_count; b += 1)
        {
            same = anchors[b] == given_anchors[b] && chosen[b] == given_chosen[b];
            // A chosen predecessor laid out before its block that the block dominates: a latch, which the tree's dominance
            // test chose.
            const uint32_t j = chosen[b];
            const uint32_t p = j != BUSTER_TIER1_CFG_NONE ? plain.predecessors[plain.predecessor_offsets[b] + j] : BUSTER_TIER1_CFG_NONE;
            tally->given_latches += shift == 0 && p != BUSTER_TIER1_CFG_NONE && p < b && dominators_dominates(&tree, b, p) ? 1 : 0;
        }
    }
    tally->given_trees += same ? 1 : 0;
    tier1_cfg_test_expect(tally, same, "anchors and chosen predecessors read from a given dominator tree, with and without an added entry, are the analysis's (#2611)");
    arena_pop_to(output, output_mark);
    arena_pop_to(scratch, scratch_mark);
}

// #2611: the given tree is the one tier1_cfg_anchors reads. The join's CFG (0 -> 1, 0 -> 2, 1 -> 2) given the tree of a
// chain 0 -> 1 -> 2, in which 1 dominates 2, anchors 2 at 1, its nearest predecessor; given a tree of another size, the
// anchors are the analysis's, 2 anchored at its dominator 0.
static void tier1_cfg_test_tree_read(Tier1CfgTestTally* tally, const Tier1CfgInput* input, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    // The chain's views, and its first two blocks' (0 -> 1).
    static const uint32_t chain_successor_offsets[2][4] = { { 0, 1, 2, 2 }, { 0, 1, 1, 0 } };
    static const uint32_t chain_successors[] = { 1, 2 };
    static const uint32_t chain_predecessor_offsets[2][4] = { { 0, 0, 1, 2 }, { 0, 0, 1, 0 } };
    static const uint32_t chain_predecessors[] = { 0, 1 };
    DominatorOptions options = dominators_options();
    options.outputs = 0;
    Dominators trees[2] = { { 0 } };
    for (uint32_t t = 0; t < 2; t += 1)
    {
        // The chain, and its first two blocks alone (another size).
        const DominatorCfg chain = {
            .block_count = 3 - t,
            .edge_count = 2 - t,
            .entry = 0,
            .successor_offsets = chain_successor_offsets[t],
            .successors = chain_successors,
            .predecessor_offsets = chain_predecessor_offsets[t],
            .predecessors = chain_predecessors,
        };
        const bool analyzed = dominators_analyze(output, scratch, &chain, options, &trees[t]) == DOMINATOR_OK;
        tier1_cfg_test_expect(tally, analyzed, "the chain's dominator tree");
    }
    uint32_t anchors[3] = { 0 };
    uint32_t chosen[3] = { 0 };
    Tier1CfgInput given = *input;
    given.dominators = &trees[0];
    given.dominator_shift = 0;
    const bool read = given.block_count == 3 && tier1_cfg_anchors(&given, output, scratch, anchors, chosen) && anchors[2] == 1 && anchors[1] == 0;
    tier1_cfg_test_expect(tally, read, "the anchors read the given tree (#2611)");
    given.dominators = &trees[1];
    const bool other = given.block_count == 3 && tier1_cfg_anchors(&given, output, scratch, anchors, chosen) && anchors[2] == 0 && anchors[1] == 0;
    tier1_cfg_test_expect(tally, other, "a given tree of another size leaves the anchors to the analysis (#2611)");
    arena_pop_to(output, output_mark);
    arena_pop_to(scratch, scratch_mark);
}

static void tier1_cfg_test_dominance(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builders[3];
    Tier1CfgTestBuilder* join = &builders[0];
    memset(join, 0, sizeof(*join));
    join->block_count = 3;
    join->instructions[0] = 2;
    join->instructions[1] = 3;
    join->instructions[2] = 2;
    tier1_cfg_test_edge(join, 0, 1);
    tier1_cfg_test_edge(join, 0, 2);
    tier1_cfg_test_edge(join, 1, 2);
    join->registers[0] = 0x000f;
    join->scratch[0] = 11;
    const uint32_t c = tier1_cfg_test_value(join, 0, 0, 0, 0x000f);
    const uint32_t t = tier1_cfg_test_value(join, 0, 1, 0, 0x000f);
    const uint32_t u = tier1_cfg_test_value(join, 1, 0, 0, 0x000f);
    const uint32_t r = tier1_cfg_test_value(join, 1, 1, 0, 0x000f);
    tier1_cfg_test_definition_use(join, c);
    tier1_cfg_test_definition_use(join, t);
    tier1_cfg_test_definition_use(join, u);
    tier1_cfg_test_definition_use(join, r);
    tier1_cfg_test_use(join, u, 1, 2, true);
    const uint32_t join_operands[] = {
        t,
        r,
    };
    const uint32_t z = tier1_cfg_test_phi(join, 2, 0, join_operands);
    tier1_cfg_test_use(join, z, 2, 0, true);
    tier1_cfg_test_use(join, c, 2, 1, true);
    Tier1CfgTestBuilder* web = &builders[1];
    memset(web, 0, sizeof(*web));
    web->block_count = 5;
    web->instructions[0] = 1;
    web->instructions[1] = 2;
    web->instructions[2] = 1;
    web->instructions[3] = 1;
    web->instructions[4] = 1;
    web->share_slots = true;
    web->scratch[0] = 11;
    tier1_cfg_test_edge(web, 0, 3);
    tier1_cfg_test_edge(web, 3, 1);
    tier1_cfg_test_edge(web, 1, 2);
    tier1_cfg_test_edge(web, 3, 2);
    tier1_cfg_test_edge(web, 2, 3);
    tier1_cfg_test_edge(web, 3, 4);
    const uint32_t e = tier1_cfg_test_value(web, 0, 0, 0, 0);
    const uint32_t q = tier1_cfg_test_value(web, 1, 0, 0, 0);
    // Block 2's predecessors are 1 and 3, block 3's 0 and 2, in the edges' order. p1 is made first, as p2's operand.
    const uint32_t p1 = web->value_count + 1;
    const uint32_t p2_operands[] = {
        q,
        p1,
    };
    const uint32_t p2 = tier1_cfg_test_phi(web, 2, 0, p2_operands);
    const uint32_t p1_operands[] = {
        e,
        p2,
    };
    const uint32_t made = tier1_cfg_test_phi(web, 3, 0, p1_operands);
    tier1_cfg_test_use(web, p1, 1, 1, false);
    tier1_cfg_test_use(web, p1, 4, 0, false);
    tier1_cfg_test_expect(tally, made == p1, "the loop's phis are made in their operands' order");
    // 0 -> 5, 5 -> 1, 5 -> 2, 1 -> 5, 2 -> 3, 3 -> 4, 4 -> 3, 4 -> 5, 5 -> 6: 5 is a join laid out after the handlers 1
    // and 2, whose dominator it is (they have no anchor), and after 1 and 4, which it dominates: its latches, of which
    // it takes the nearer, 4. The loop 3 -> 4 -> 3 has its header first: 3's anchor is its dominator 2, 4's is 3.
    Tier1CfgTestBuilder* shapes = &builders[2];
    memset(shapes, 0, sizeof(*shapes));
    shapes->block_count = 7;
    for (uint32_t b = 0; b < shapes->block_count; b += 1)
    {
        shapes->instructions[b] = 1;
    }
    shapes->registers[0] = 0x000f;
    shapes->scratch[0] = 11;
    tier1_cfg_test_edge(shapes, 0, 5);
    tier1_cfg_test_edge(shapes, 5, 1);
    tier1_cfg_test_edge(shapes, 5, 2);
    tier1_cfg_test_edge(shapes, 1, 5);
    tier1_cfg_test_edge(shapes, 2, 3);
    tier1_cfg_test_edge(shapes, 3, 4);
    tier1_cfg_test_edge(shapes, 4, 3);
    tier1_cfg_test_edge(shapes, 4, 5);
    tier1_cfg_test_edge(shapes, 5, 6);
    const uint32_t none = BUSTER_TIER1_CFG_NONE;
    const uint32_t join_anchors[] = { none, 0, 0 };
    const uint32_t join_chosen[] = { none, 0, 0 };
    const uint32_t web_anchors[] = { none, none, none, 2, 3 };
    const uint32_t web_chosen[] = { none, none, 1, 2, 3 };
    const uint32_t shape_anchors[] = { none, none, none, 2, 3, 4, 5 };
    const uint32_t shape_chosen[] = { none, none, none, 2, 3, 4, 5 };
    const uint32_t* anchors_expected[] = { join_anchors, web_anchors, shape_anchors };
    const uint32_t* chosen_expected[] = { join_chosen, web_chosen, shape_chosen };
    const char* names[] = {
        "a join's phi takes its dominating predecessor's operand's register",
        "a phi web around a loop keeps one slot",
        "anchors and chosen predecessors follow dominators and latches",
    };
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(builders); f += 1)
    {
        const uint64_t saved = output->length;
        Tier1CfgInput input;
        const bool built = tier1_cfg_test_build(&builders[f], output, &input);
        tier1_cfg_test_expect(tally, built, names[f]);
        if (built)
        {
            tier1_cfg_test_anchors(tally, &input, output, scratch, anchors_expected[f], chosen_expected[f], names[f]);
            tier1_cfg_test_given_tree(tally, &input, output, scratch);
        }
        if (built && f == 0)
        {
            tier1_cfg_test_tree_read(tally, &input, output, scratch);
        }
        if (built && f < 2)
        {
            const uint64_t mark = output->length;
            Tier1CfgAllocation result = { 0 };
            const Tier1CfgError error = tier1_cfg_allocate(&input, output, scratch, &result);
            Tier1CfgCheck check;
            const bool allocated = error == TIER1_CFG_OK && tier1_cfg_verify(&input, &result, scratch, &check);
            tier1_cfg_test_expect(tally, allocated, names[f]);
            const bool quiet = f == 0 ? tier1_cfg_test_edge_moves(&input, &result, 0, 2) == 0 : tier1_cfg_test_edge_moves(&input, &result, 2, 3) == 0 && tier1_cfg_test_edge_moves(&input, &result, 3, 2) == 0;
            tier1_cfg_test_expect(tally, allocated && quiet, f == 0 ? "the dominating predecessor's critical edge moves nothing" : "the loop's edges move nothing for its phi web");
            arena_pop_to(output, mark);
        }
        if (built && f < 2)
        {
            tier1_cfg_test_case(tally, &input, output, scratch, names[f], true, 0, 0, true);
        }
        arena_pop_to(output, saved);
    }
}

// #2784's fixture, Lua's str_gsub loop in small: 0 -> 6, 1 -> 2, 4 -> 2, 2 -> 5, 2 -> 9, 3 -> 4, 5 -> 6, 6 -> 7, 7 -> 3,
// 7 -> 8, 8 -> 1, 8 -> 9. The header 6 follows its body: p = phi(s, j) at 6 takes j's register at the exit of its latch 5,
// and 5 j's at the exit of the join 2, its anchor, where the loop may end. j = phi(e, q) at 2 takes e's register at the
// exit of 1, the matches' path, which reads e, defined in 7, and p, and is 2's nearest predecessor in layout. The
// characters' path reads p in 3, the first successor of 7, and defines q = p + 1 in 4. p's range at 3, before its
// definition and without an anchor or a sibling to follow, took p's last register, from 1, where e held the one j then
// took, and the edge 7 -> 3 moved p from the header's register into it on every iteration. It now takes j's register at
// 2's exit (tier1_cfg_body_hints), and the characters' path moves nothing.
static void tier1_cfg_test_body_hints(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builder;
    memset(&builder, 0, sizeof(builder));
    builder.block_count = 10;
    const uint32_t instructions[] = { 1, 1, 1, 1, 2, 1, 1, 2, 1, 1 };
    for (uint32_t b = 0; b < builder.block_count; b += 1)
    {
        builder.instructions[b] = instructions[b];
    }
    builder.registers[0] = 0x000f;
    builder.scratch[0] = 11;
    const uint32_t edges[][2] = {
        { 0, 6 },
        { 1, 2 },
        { 4, 2 },
        { 2, 5 },
        { 2, 9 },
        { 3, 4 },
        { 5, 6 },
        { 6, 7 },
        { 7, 3 },
        { 7, 8 },
        { 8, 1 },
        { 8, 9 },
    };
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(edges); k += 1)
    {
        tier1_cfg_test_edge(&builder, edges[k][0], edges[k][1]);
    }
    const uint32_t source = tier1_cfg_test_value(&builder, 0, 0, 0, 0x000f);
    const uint32_t e = tier1_cfg_test_value(&builder, 7, 1, 0, 0x000f);
    const uint32_t q = tier1_cfg_test_value(&builder, 4, 1, 0, 0x000f);
    tier1_cfg_test_definition_use(&builder, source);
    tier1_cfg_test_definition_use(&builder, e);
    tier1_cfg_test_definition_use(&builder, q);
    // Block 2's predecessors are 1 and 4, block 6's 0 and 5, in the edges' order.
    const uint32_t join_operands[] = {
        e,
        q,
    };
    const uint32_t j = tier1_cfg_test_phi(&builder, 2, 0, join_operands);
    const uint32_t header_operands[] = {
        source,
        j,
    };
    const uint32_t p = tier1_cfg_test_phi(&builder, 6, 0, header_operands);
    tier1_cfg_test_use(&builder, p, 1, 0, true);
    tier1_cfg_test_use(&builder, e, 1, 0, true);
    tier1_cfg_test_use(&builder, p, 3, 0, true);
    tier1_cfg_test_use(&builder, p, 4, 0, true);
    tier1_cfg_test_use(&builder, p, 7, 0, true);
    const uint64_t saved = output->length;
    Tier1CfgInput input;
    const bool built = tier1_cfg_test_build(&builder, output, &input);
    tier1_cfg_test_expect(tally, built, "a loop's body before its header: built");
    if (built)
    {
        const uint64_t mark = output->length;
        Tier1CfgAllocation result = { 0 };
        const Tier1CfgError error = tier1_cfg_allocate(&input, output, scratch, &result);
        Tier1CfgCheck check;
        const bool allocated = error == TIER1_CFG_OK && tier1_cfg_verify(&input, &result, scratch, &check);
        tier1_cfg_test_expect(tally, allocated, "a loop's body before its header: allocated");
        const bool quiet = tier1_cfg_test_edge_moves(&input, &result, 7, 3) == 0 && tier1_cfg_test_edge_moves(&input, &result, 4, 2) == 0 && tier1_cfg_test_edge_moves(&input, &result, 2, 5) == 0 &&
                           tier1_cfg_test_edge_moves(&input, &result, 5, 6) == 0;
        tier1_cfg_test_expect(tally, allocated && quiet, "a loop's body before its header takes its phi's register: the characters' path moves nothing");
        arena_pop_to(output, mark);
    }
    if (built)
    {
        tier1_cfg_test_case(tally, &input, output, scratch, "a loop's body before its header", true, 0, 0, true);
    }
    arena_pop_to(output, saved);
}

static void tier1_cfg_test_fixtures(Tier1CfgTestTally* tally, Arena* output, Arena* scratch)
{
    static Tier1CfgTestBuilder builders[11];
    Tier1CfgTestBuilder* diamond = &builders[0];
    // Diamond: 0 -> 1, 0 -> 2, 1 -> 3, 2 -> 3; x in 1 and y in 2 meet in phi z at 3, beside a, live through.
    memset(diamond, 0, sizeof(*diamond));
    diamond->block_count = 4;
    diamond->instructions[0] = 2;
    diamond->instructions[1] = 2;
    diamond->instructions[2] = 2;
    diamond->instructions[3] = 2;
    tier1_cfg_test_edge(diamond, 0, 1);
    tier1_cfg_test_edge(diamond, 0, 2);
    tier1_cfg_test_edge(diamond, 1, 3);
    tier1_cfg_test_edge(diamond, 2, 3);
    diamond->registers[0] = 0x00ff;
    diamond->registers[1] = 0x000f;
    diamond->scratch[0] = 11;
    diamond->scratch[1] = 15;
    const uint32_t a = tier1_cfg_test_value(diamond, 0, 0, 0, 0x00ff);
    const uint32_t x = tier1_cfg_test_value(diamond, 1, 0, 0, 0x00ff);
    const uint32_t y = tier1_cfg_test_value(diamond, 2, 0, 0, 0x00ff);
    tier1_cfg_test_use(diamond, a, 1, 0, true);
    tier1_cfg_test_use(diamond, a, 2, 0, true);
    const uint32_t diamond_operands[] = {
        x,
        y,
    };
    const uint32_t z = tier1_cfg_test_phi(diamond, 3, 0, diamond_operands);
    tier1_cfg_test_use(diamond, z, 3, 0, true);
    tier1_cfg_test_use(diamond, a, 3, 1, true);
    // Loop: 0 -> 1 -> 2 -> 1, 2 -> 3; i = phi(init, next) at 1, next = i + step in 2, i used after the loop.
    Tier1CfgTestBuilder* loop = &builders[1];
    memset(loop, 0, sizeof(*loop));
    loop->block_count = 4;
    loop->instructions[0] = 2;
    loop->instructions[1] = 1;
    loop->instructions[2] = 2;
    loop->instructions[3] = 1;
    tier1_cfg_test_edge(loop, 0, 1);
    tier1_cfg_test_edge(loop, 2, 1);
    tier1_cfg_test_edge(loop, 1, 2);
    tier1_cfg_test_edge(loop, 2, 3);
    loop->registers[0] = 0x000f;
    loop->registers[1] = 0x000f;
    loop->scratch[0] = 11;
    loop->scratch[1] = 15;
    const uint32_t init = tier1_cfg_test_value(loop, 0, 0, 0, 0x000f);
    const uint32_t step = tier1_cfg_test_value(loop, 0, 1, 0, 0x000f);
    const uint32_t next = tier1_cfg_test_value(loop, 2, 0, 0, 0x000f);
    const uint32_t loop_operands[] = {
        init,
        next,
    };
    const uint32_t i = tier1_cfg_test_phi(loop, 1, 0, loop_operands);
    tier1_cfg_test_use(loop, i, 2, 0, true);
    tier1_cfg_test_use(loop, step, 2, 0, true);
    tier1_cfg_test_use(loop, i, 3, 0, true);
    // Swap: a loop whose phis exchange two values every iteration, a register cycle on the backedge.
    Tier1CfgTestBuilder* swap = &builders[2];
    memset(swap, 0, sizeof(*swap));
    swap->block_count = 3;
    swap->instructions[0] = 2;
    swap->instructions[1] = 2;
    swap->instructions[2] = 1;
    tier1_cfg_test_edge(swap, 0, 1);
    tier1_cfg_test_edge(swap, 1, 1);
    tier1_cfg_test_edge(swap, 1, 2);
    swap->registers[0] = 0x0003;
    swap->registers[1] = 0x0001;
    swap->scratch[0] = 11;
    swap->scratch[1] = 15;
    const uint32_t first = tier1_cfg_test_value(swap, 0, 0, 0, 0x0003);
    const uint32_t second = tier1_cfg_test_value(swap, 0, 1, 0, 0x0003);
    const uint32_t swap_a_operands[] = {
        first,
        swap->value_count + 1,
    };
    const uint32_t swap_a = tier1_cfg_test_phi(swap, 1, 0, swap_a_operands);
    const uint32_t swap_b_operands[] = {
        second,
        swap_a,
    };
    const uint32_t swap_b = tier1_cfg_test_phi(swap, 1, 0, swap_b_operands);
    tier1_cfg_test_use(swap, swap_a, 1, 0, true);
    tier1_cfg_test_use(swap, swap_b, 1, 1, true);
    tier1_cfg_test_use(swap, swap_a, 2, 0, true);
    tier1_cfg_test_use(swap, swap_b, 2, 0, true);
    // Critical edge: 0 -> 1, 0 -> 2, 1 -> 2: the edge 0 -> 2 leaves a branch and enters a join; w in 0 must be moved
    // on it when its registers differ, which one register and a value defined in 1 make likely.
    Tier1CfgTestBuilder* critical = &builders[3];
    memset(critical, 0, sizeof(*critical));
    critical->block_count = 3;
    critical->instructions[0] = 2;
    critical->instructions[1] = 2;
    critical->instructions[2] = 1;
    tier1_cfg_test_edge(critical, 0, 1);
    tier1_cfg_test_edge(critical, 0, 2);
    tier1_cfg_test_edge(critical, 1, 2);
    critical->registers[0] = 0x0003;
    critical->registers[1] = 0x0001;
    critical->scratch[0] = 11;
    critical->scratch[1] = 15;
    const uint32_t w = tier1_cfg_test_value(critical, 0, 0, 0, 0x0003);
    const uint32_t c0 = tier1_cfg_test_value(critical, 0, 1, 0, 0x0003);
    const uint32_t c1 = tier1_cfg_test_value(critical, 1, 0, 0, 0x0003);
    tier1_cfg_test_use(critical, c0, 1, 0, true);
    tier1_cfg_test_use(critical, w, 1, 1, true);
    const uint32_t critical_operands[] = {
        c0,
        c1,
    };
    const uint32_t joined = tier1_cfg_test_phi(critical, 2, 0, critical_operands);
    tier1_cfg_test_use(critical, joined, 2, 0, true);
    tier1_cfg_test_use(critical, w, 2, 0, true);
    // One register: every value but one is spilled across the edges; phis move memory to memory through scratch.
    Tier1CfgTestBuilder* tight = &builders[4];
    memset(tight, 0, sizeof(*tight));
    tight->block_count = 4;
    tight->instructions[0] = 3;
    tight->instructions[1] = 2;
    tight->instructions[2] = 2;
    tight->instructions[3] = 2;
    tier1_cfg_test_edge(tight, 0, 1);
    tier1_cfg_test_edge(tight, 0, 2);
    tier1_cfg_test_edge(tight, 1, 3);
    tier1_cfg_test_edge(tight, 2, 3);
    tight->registers[0] = 0x0001;
    tight->registers[1] = 0x0001;
    tight->scratch[0] = 11;
    tight->scratch[1] = 15;
    const uint32_t t0 = tier1_cfg_test_value(tight, 0, 0, 0, 0x0001);
    const uint32_t t1 = tier1_cfg_test_value(tight, 0, 1, 0, 0x0001);
    const uint32_t t2 = tier1_cfg_test_value(tight, 0, 2, 0, 0x0001);
    tier1_cfg_test_use(tight, t0, 1, 0, true);
    tier1_cfg_test_use(tight, t1, 2, 0, true);
    const uint32_t tight_operands[] = {
        t0,
        t1,
    };
    const uint32_t tight_phi = tier1_cfg_test_phi(tight, 3, 0, tight_operands);
    tier1_cfg_test_use(tight, tight_phi, 3, 0, true);
    tier1_cfg_test_use(tight, t2, 3, 1, true);
    // Memory swap: no general registers, so the swap's phis go slot to slot, a cycle through the temporary slot.
    Tier1CfgTestBuilder* memory = &builders[5];
    memset(memory, 0, sizeof(*memory));
    memory->block_count = 3;
    memory->instructions[0] = 2;
    memory->instructions[1] = 2;
    memory->instructions[2] = 1;
    tier1_cfg_test_edge(memory, 0, 1);
    tier1_cfg_test_edge(memory, 1, 1);
    tier1_cfg_test_edge(memory, 1, 2);
    memory->registers[0] = 0;
    memory->registers[1] = 0x0001;
    memory->scratch[0] = 11;
    memory->scratch[1] = 15;
    const uint32_t m0 = tier1_cfg_test_value(memory, 0, 0, 0, 0);
    const uint32_t m1 = tier1_cfg_test_value(memory, 0, 1, 0, 0);
    const uint32_t memory_a_operands[] = {
        m0,
        memory->value_count + 1,
    };
    const uint32_t memory_a = tier1_cfg_test_phi(memory, 1, 0, memory_a_operands);
    const uint32_t memory_b_operands[] = {
        m1,
        memory_a,
    };
    const uint32_t memory_b = tier1_cfg_test_phi(memory, 1, 0, memory_b_operands);
    tier1_cfg_test_use(memory, memory_a, 1, 0, false);
    tier1_cfg_test_use(memory, memory_b, 1, 1, false);
    tier1_cfg_test_use(memory, memory_a, 2, 0, false);
    tier1_cfg_test_use(memory, memory_b, 2, 0, false);
    // A call across a double: a vector value live across a fixed range that clobbers every vector register, with a
    // use after it that needs one, which the whole range couldn't have. Its range is cut around the call: stored
    // before it and reloaded after, at two splits.
    Tier1CfgTestBuilder* across = &builders[6];
    memset(across, 0, sizeof(*across));
    across->block_count = 1;
    across->instructions[0] = 3;
    across->registers[0] = 0x0003;
    across->registers[1] = 0x000f;
    across->scratch[0] = 11;
    across->scratch[1] = 15;
    const uint32_t d = tier1_cfg_test_value(across, 0, 0, 1, 0x000f);
    across->fixed_block[0] = 0;
    across->fixed_instruction[0] = 1;
    across->fixed_class[0] = 1;
    across->fixed_register[0] = 0x000f;
    across->fixed_count = 1;
    tier1_cfg_test_use(across, d, 0, 2, true);
    // The opmask class (#179), its registers k1 to k7 at most and k0 its scratch. Mask swap: the phi swap with k1 and k2
    // alone, whose back edge's cycle goes through k0.
    Tier1CfgTestBuilder* mask_swap = &builders[7];
    memset(mask_swap, 0, sizeof(*mask_swap));
    mask_swap->block_count = 3;
    mask_swap->instructions[0] = 2;
    mask_swap->instructions[1] = 2;
    mask_swap->instructions[2] = 1;
    tier1_cfg_test_edge(mask_swap, 0, 1);
    tier1_cfg_test_edge(mask_swap, 1, 1);
    tier1_cfg_test_edge(mask_swap, 1, 2);
    mask_swap->registers[BUSTER_TIER1_ALLOCATE_GPR] = 0x0003;
    mask_swap->registers[BUSTER_TIER1_ALLOCATE_VECTOR] = 0x0001;
    mask_swap->registers[BUSTER_TIER1_ALLOCATE_MASK] = 0x06;
    mask_swap->scratch[BUSTER_TIER1_ALLOCATE_GPR] = 11;
    mask_swap->scratch[BUSTER_TIER1_ALLOCATE_VECTOR] = 15;
    mask_swap->scratch[BUSTER_TIER1_ALLOCATE_MASK] = 0;
    const uint32_t k_first = tier1_cfg_test_value(mask_swap, 0, 0, BUSTER_TIER1_ALLOCATE_MASK, 0x06);
    const uint32_t k_second = tier1_cfg_test_value(mask_swap, 0, 1, BUSTER_TIER1_ALLOCATE_MASK, 0x06);
    const uint32_t mask_swap_a_operands[] = {
        k_first,
        mask_swap->value_count + 1,
    };
    const uint32_t mask_swap_a = tier1_cfg_test_phi(mask_swap, 1, BUSTER_TIER1_ALLOCATE_MASK, mask_swap_a_operands);
    const uint32_t mask_swap_b_operands[] = {
        k_second,
        mask_swap_a,
    };
    const uint32_t mask_swap_b = tier1_cfg_test_phi(mask_swap, 1, BUSTER_TIER1_ALLOCATE_MASK, mask_swap_b_operands);
    tier1_cfg_test_use(mask_swap, mask_swap_a, 1, 0, true);
    tier1_cfg_test_use(mask_swap, mask_swap_b, 1, 1, true);
    tier1_cfg_test_use(mask_swap, mask_swap_a, 2, 0, true);
    tier1_cfg_test_use(mask_swap, mask_swap_b, 2, 0, true);
    // Mask memory swap: no opmask registers, so the swap's phis go slot to slot, a cycle through the mask class's
    // temporary slot, each slot-to-slot move through k0.
    Tier1CfgTestBuilder* mask_memory = &builders[8];
    memset(mask_memory, 0, sizeof(*mask_memory));
    mask_memory->block_count = 3;
    mask_memory->instructions[0] = 2;
    mask_memory->instructions[1] = 2;
    mask_memory->instructions[2] = 1;
    tier1_cfg_test_edge(mask_memory, 0, 1);
    tier1_cfg_test_edge(mask_memory, 1, 1);
    tier1_cfg_test_edge(mask_memory, 1, 2);
    mask_memory->registers[BUSTER_TIER1_ALLOCATE_GPR] = 0x0003;
    mask_memory->registers[BUSTER_TIER1_ALLOCATE_VECTOR] = 0x0001;
    mask_memory->registers[BUSTER_TIER1_ALLOCATE_MASK] = 0;
    mask_memory->scratch[BUSTER_TIER1_ALLOCATE_GPR] = 11;
    mask_memory->scratch[BUSTER_TIER1_ALLOCATE_VECTOR] = 15;
    mask_memory->scratch[BUSTER_TIER1_ALLOCATE_MASK] = 0;
    const uint32_t km0 = tier1_cfg_test_value(mask_memory, 0, 0, BUSTER_TIER1_ALLOCATE_MASK, 0);
    const uint32_t km1 = tier1_cfg_test_value(mask_memory, 0, 1, BUSTER_TIER1_ALLOCATE_MASK, 0);
    const uint32_t mask_memory_a_operands[] = {
        km0,
        mask_memory->value_count + 1,
    };
    const uint32_t mask_memory_a = tier1_cfg_test_phi(mask_memory, 1, BUSTER_TIER1_ALLOCATE_MASK, mask_memory_a_operands);
    const uint32_t mask_memory_b_operands[] = {
        km1,
        mask_memory_a,
    };
    const uint32_t mask_memory_b = tier1_cfg_test_phi(mask_memory, 1, BUSTER_TIER1_ALLOCATE_MASK, mask_memory_b_operands);
    tier1_cfg_test_use(mask_memory, mask_memory_a, 1, 0, false);
    tier1_cfg_test_use(mask_memory, mask_memory_b, 1, 1, false);
    tier1_cfg_test_use(mask_memory, mask_memory_a, 2, 0, false);
    tier1_cfg_test_use(mask_memory, mask_memory_b, 2, 0, false);
    // Masks across a call, which clobbers every opmask register, with uses after it that need registers: each mask's
    // range is cut around the call, stored before it and reloaded after, four moves at the two splits; a general value
    // live across it keeps its register.
    Tier1CfgTestBuilder* mask_call = &builders[9];
    memset(mask_call, 0, sizeof(*mask_call));
    mask_call->block_count = 1;
    mask_call->instructions[0] = 3;
    mask_call->registers[BUSTER_TIER1_ALLOCATE_GPR] = 0x0003;
    mask_call->registers[BUSTER_TIER1_ALLOCATE_VECTOR] = 0x000f;
    mask_call->registers[BUSTER_TIER1_ALLOCATE_MASK] = 0xfe;
    mask_call->scratch[BUSTER_TIER1_ALLOCATE_GPR] = 11;
    mask_call->scratch[BUSTER_TIER1_ALLOCATE_VECTOR] = 15;
    mask_call->scratch[BUSTER_TIER1_ALLOCATE_MASK] = 0;
    const uint32_t kc0 = tier1_cfg_test_value(mask_call, 0, 0, BUSTER_TIER1_ALLOCATE_MASK, 0xfe);
    const uint32_t kc1 = tier1_cfg_test_value(mask_call, 0, 0, BUSTER_TIER1_ALLOCATE_MASK, 0xfe);
    const uint32_t kept = tier1_cfg_test_value(mask_call, 0, 0, BUSTER_TIER1_ALLOCATE_GPR, 0x0003);
    mask_call->fixed_block[0] = 0;
    mask_call->fixed_instruction[0] = 1;
    mask_call->fixed_class[0] = BUSTER_TIER1_ALLOCATE_MASK;
    mask_call->fixed_register[0] = 0xfe;
    mask_call->fixed_count = 1;
    tier1_cfg_test_use(mask_call, kc0, 0, 2, true);
    tier1_cfg_test_use(mask_call, kc1, 0, 2, true);
    tier1_cfg_test_use(mask_call, kept, 0, 2, true);
    // Eight masks in seven registers: m_j defined at instruction j and used at 8 + j, so that all eight are live at
    // instruction 8, where one must be in its slot.
    Tier1CfgTestBuilder* mask_pressure = &builders[10];
    memset(mask_pressure, 0, sizeof(*mask_pressure));
    mask_pressure->block_count = 1;
    mask_pressure->instructions[0] = 16;
    mask_pressure->registers[BUSTER_TIER1_ALLOCATE_GPR] = 0x0003;
    mask_pressure->registers[BUSTER_TIER1_ALLOCATE_VECTOR] = 0x0001;
    mask_pressure->registers[BUSTER_TIER1_ALLOCATE_MASK] = 0xfe;
    mask_pressure->scratch[BUSTER_TIER1_ALLOCATE_GPR] = 11;
    mask_pressure->scratch[BUSTER_TIER1_ALLOCATE_VECTOR] = 15;
    mask_pressure->scratch[BUSTER_TIER1_ALLOCATE_MASK] = 0;
    for (uint32_t j = 0; j < 8; j += 1)
    {
        const uint32_t mask = tier1_cfg_test_value(mask_pressure, 0, j, BUSTER_TIER1_ALLOCATE_MASK, 0xfe);
        tier1_cfg_test_use(mask_pressure, mask, 0, 8 + j, true);
    }
    const char* names[] = {
        "diamond",
        "loop",
        "phi swap",
        "critical edge",
        "one register",
        "memory swap",
        "call across a double",
        "mask phi swap",
        "mask memory swap",
        "masks across a call",
        "eight masks in seven registers",
    };
    const uint8_t temp_slots[] = {
        0,
        0,
        0,
        0,
        0,
        1u << BUSTER_TIER1_ALLOCATE_GPR,
        0,
        0,
        1u << BUSTER_TIER1_ALLOCATE_MASK,
        0,
        0,
    };
    const uint32_t split_moves[] = {
        0,
        0,
        0,
        0,
        0,
        0,
        2,
        0,
        0,
        4,
        0,
    };
    // What the opmask fixtures must do: move masks through k0, the class's scratch register, and spill masks.
    const bool mask_scratch[] = {
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        true,
        true,
        false,
        false,
    };
    const bool mask_spill[] = {
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        true,
        true,
        true,
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(names) == BUSTER_ARRAY_LENGTH(builders), "a name per fixture");
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(names); f += 1)
    {
        const uint64_t saved = output->length;
        Tier1CfgInput input;
        const bool built = tier1_cfg_test_build(&builders[f], output, &input);
        tier1_cfg_test_expect(tally, built, names[f]);
        if (built)
        {
            const uint64_t scratch_before = tally->scratch_moves;
            const uint64_t spills_before = tally->mask_spills;
            tier1_cfg_test_case(tally, &input, output, scratch, names[f], true, temp_slots[f], split_moves[f], true);
            tier1_cfg_test_expect(tally, !mask_scratch[f] || tally->scratch_moves > scratch_before, "the opmask fixture moves a mask through k0");
            tier1_cfg_test_expect(tally, !mask_spill[f] || tally->mask_spills > spills_before, "the opmask fixture spills a mask");
        }
        arena_pop_to(output, saved);
    }
    // #179's review: the mask phi swap with k0 among its class's registers and the scratch register elsewhere. No mask
    // value may take k0, so both paths refuse the input, leaving the output arena as it was.
    Tier1CfgTestBuilder* k0_swap = &builders[7];
    k0_swap->registers[BUSTER_TIER1_ALLOCATE_MASK] = 0x07;
    k0_swap->scratch[BUSTER_TIER1_ALLOCATE_MASK] = 3;
    const uint64_t k0_saved = output->length;
    Tier1CfgInput k0_input;
    const bool k0_built = tier1_cfg_test_build(k0_swap, output, &k0_input);
    tier1_cfg_test_expect(tally, k0_built, "the k0 fixture builds");
    if (k0_built)
    {
        const uint64_t before = output->length;
        Tier1CfgAllocation result = { 0 };
        const Tier1CfgError error = tier1_cfg_allocate(&k0_input, output, scratch, &result);
        tier1_cfg_test_expect(tally, error == TIER1_CFG_INVALID && output->length == before, "a mask class with k0 among its registers is refused");
    }
    arena_pop_to(output, k0_saved);
    (void)z;
    (void)i;
    (void)swap_b;
    (void)memory_b;
    (void)mask_swap_b;
    (void)mask_memory_b;
}

#include "tier1_cfg_spill_test.c"
#include "tier1_cfg_edges_test.c"

static __attribute__((unused)) int run_tier1_cfg_tests(int argc, char** argv)
{
    uint64_t seed = 140;
    uint64_t count = 2000;
    uint64_t oracle_every = 16;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &seed);
        }
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &count) && count <= 1000000;
        }
        else if (strcmp(argv[i], "--oracle-every") == 0 && i + 1 < argc)
        {
            i += 1;
            valid = dominator_test_parse(argv[i], &oracle_every) && oracle_every != 0;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-tier1-cfg [--seed N] [--count N] [--oracle-every N]\n");
    }
    else
    {
        Arena output = arena_reserve(BUSTER_TIER1_CFG_TEST_ARENA);
        Arena scratch = arena_reserve(BUSTER_TIER1_CFG_TEST_ARENA);
        Arena work = arena_reserve(BUSTER_TIER1_CFG_TEST_ARENA);
        Tier1CfgTestTally tally = { 0 };
        tier1_cfg_test_expect(&tally, output.base != 0 && scratch.base != 0 && work.base != 0, "test arenas");
        if (output.base != 0 && scratch.base != 0 && work.base != 0)
        {
            tally.oracle = true;
            tally.oracle_mutants = true;
            tier1_cfg_test_fixtures(&tally, &output, &scratch);
            tier1_cfg_test_undefined(&tally, &output, &scratch);
            tier1_cfg_test_dominance(&tally, &output, &scratch);
            tier1_cfg_test_body_hints(&tally, &output, &scratch);
            tier1_cfg_test_local_phis(&tally, &output, &scratch);
            tier1_cfg_test_dispatch_entry(&tally, &output, &scratch);
            tier1_cfg_test_phi_storage(&tally, &output, &scratch);
            tier1_cfg_test_current_slots(&tally, &output, &scratch);
            tier1_cfg_test_edge_views(&tally, &scratch);
            tier1_cfg_test_wide_edges(&tally, &output, &scratch);
            tier1_cfg_test_use_blocks(&tally, &scratch);
            Tier1CfgTestBuilder* builder = arena_push(&work, sizeof(Tier1CfgTestBuilder), _Alignof(Tier1CfgTestBuilder));
            tier1_cfg_test_expect(&tally, builder != 0, "builder memory");
            for (uint64_t sample = 0; builder != 0 && sample < count; sample += 1)
            {
                const uint64_t work_saved = work.length;
                const uint64_t output_saved = output.length;
                const bool generated = tier1_cfg_test_generate(builder, seed * 1000003 + sample, &work, &scratch);
                tier1_cfg_test_expect(&tally, generated, "generated case");
                Tier1CfgInput input;
                const bool built = generated && tier1_cfg_test_build(builder, &output, &input);
                tier1_cfg_test_expect(&tally, !generated || built, "generated case input");
                if (built)
                {
                    tally.oracle = sample % oracle_every == 0;
                    tally.oracle_mutants = sample % (oracle_every * 4) == 0;
                    tier1_cfg_test_case(&tally, &input, &output, &scratch, "generated case", sample % 8 == 0, 0, 0, false);
                    tier1_cfg_test_given_tree(&tally, &input, &output, &scratch);
                }
                arena_pop_to(&output, output_saved);
                arena_pop_to(&work, work_saved);
            }
        }
        // Every mutant must fail the checker, and a full run must reach each kind of mutant, a split edge, a cycle or
        // slot-to-slot move through the scratch register and a temporary slot.
        tier1_cfg_test_expect(&tally, tally.interference_killed == tally.interference_mutants && tally.order_killed == tally.order_mutants && tally.definition_killed == tally.definition_mutants, "every mutant fails the checker");
        tier1_cfg_test_expect(&tally, tally.destination_killed == tally.destination_mutants && tally.k0_killed == tally.k0_mutants, "every split destination and k0 mutant fails the checker");
        tier1_cfg_test_expect(&tally, tally.slot_killed == tally.slot_mutants && (count < 500 || (tally.slot_mutants != 0 && tally.shared_slots != 0)), "phi webs share slots, and every shared-slot mutant fails the checker");
        tier1_cfg_test_expect(&tally, tally.definition_mutants != 0, "the random programs' required definitions reach the definition mutant");
        tier1_cfg_test_expect(&tally, count < 500 || (tally.interference_mutants != 0 && tally.order_mutants != 0 && tally.destination_mutants != 0 && tally.k0_mutants != 0 && tally.split_edges != 0 && tally.split_moves != 0 && tally.scratch_moves != 0 && tally.temp_slot_cases != 0), "the cases reach every kind of move and mutant");
        tier1_cfg_test_expect(&tally, tally.mask_moves != 0 && tally.mask_spills != 0 && tally.mask_split_moves != 0, "the opmask class moves, spills and splits masks");
        tier1_cfg_test_expect(&tally, count < 100 || tally.undefined_operands != 0, "the cases reach undefined phi operands");
        tier1_cfg_test_expect(&tally, count < 500 || tally.unread_entries != 0, "the cases reach block entries whose recomputed value's register reads nothing");
        tier1_cfg_test_expect(&tally, count < 500 || (tally.avoiding_ranges != 0 && tally.reading_ranges != 0), "the cases reach ranges that avoid fixed sequences' registers and readers across their sequences");
        tier1_cfg_test_expect(&tally, tally.carried_killed == tally.carried_mutants && (count < 500 || (tally.carried_entries != 0 && tally.carried_mutants != 0)),
                              "the cases reach entries the carried proof takes and ones it refuses, and every refused one marked fails the checker");
        // #939: every allocation's certificate proved, and the oracle reached some of them.
        tier1_cfg_test_expect(&tally, tally.certificates == tally.allocations && tally.oracle_certificates != 0, "every allocation's certificate is proved, the oracle's share among them");
        // A mutant that moves a value wrongly must fail the certificate too: every order, definition and split
        // destination mutant. An interference or k0 mutant may only break a constraint the verifier checks (two SSA
        // names of equal contents in one register at a block's start, a mask in k0 for a fragment nothing reads), and
        // the certificate never refuses what the verifier accepts.
        tier1_cfg_test_expect(&tally, tally.order_refused == tally.order_mutants && tally.definition_refused == tally.definition_mutants && tally.destination_refused == tally.destination_mutants, "every order, definition and split destination mutant fails the certificate");
        tier1_cfg_test_expect(&tally, tally.certificate_only == 0 && tally.verifier_only == (tally.interference_mutants - tally.interference_refused) + (tally.k0_mutants - tally.k0_refused) + (tally.slot_mutants - tally.slot_refused) + (tally.carried_mutants - tally.carried_refused), "the certificate refuses no mutant the verifier accepts");
        // #2538: the one-call order reaches a taken preference, a refused one, a constraint that frees the definitions and a
        // case with fixed ranges.
        tier1_cfg_test_expect(&tally, count < 500 || (tally.two_taken != 0 && tally.two_refused != 0 && tally.two_freed != 0 && tally.two_fixed != 0), "two problems reach every outcome");
        // The column's set checks probed past a step of 16 values and into the next's tail.
        tier1_cfg_test_expect(&tally, count < 500 || tally.column_widest > 16, "the column's set checks reach a second step of values");
        // A malformed shape must fail both, before either reads through it (#1142's review).
        tier1_cfg_test_expect(&tally, tally.shape_mutants != 0 && tally.shape_killed == tally.shape_mutants && tally.shape_refused == tally.shape_mutants, "every malformed shape fails both checkers");
        printf("test-tier1-cfg: %" PRIu64 " checks, %" PRIu64 " failures; %" PRIu64 " allocations, %" PRIu64 " constraint refusals; %" PRIu64 " moves, %" PRIu64 " at splits, %" PRIu64 " through the scratch register, %" PRIu64 " split edges, %" PRIu64 " spilled values, %" PRIu64 " allocations with a temporary slot; %" PRIu64 " mask moves, %" PRIu64 " at splits, %" PRIu64 " spilled masks; mutants rejected: %" PRIu64 " of %" PRIu64 " interference, %" PRIu64 " of %" PRIu64 " order, %" PRIu64 " of %" PRIu64 " definition, %" PRIu64 " of %" PRIu64 " split destination, %" PRIu64 " of %" PRIu64 " k0, %" PRIu64 " of %" PRIu64 " shared slot; %" PRIu64 " values in a phi web's slot, %" PRIu64 " entries left to their recipes, %" PRIu64 " carried, %" PRIu64 " of %" PRIu64 " carried mutants rejected (seed=%" PRIu64 ", count=%" PRIu64 ")\n", tally.checks, tally.failures, tally.allocations, tally.constraints, tally.moves, tally.split_moves, tally.scratch_moves, tally.split_edges, tally.spills, tally.temp_slot_cases, tally.mask_moves, tally.mask_split_moves, tally.mask_spills, tally.interference_killed, tally.interference_mutants, tally.order_killed, tally.order_mutants, tally.definition_killed, tally.definition_mutants, tally.destination_killed, tally.destination_mutants, tally.k0_killed, tally.k0_mutants, tally.slot_killed, tally.slot_mutants, tally.shared_slots, tally.unread_entries, tally.carried_entries, tally.carried_killed, tally.carried_mutants, seed, count);
        printf("test-tier1-cfg: certificates: %" PRIu64 " proved (%" PRIu64 " by the oracle too), %" PRIu64 " facts, %" PRIu64 " edits, %" PRIu64 " operands, %" PRIu64 " renames; mutants refused: %" PRIu64 " interference, %" PRIu64 " order, %" PRIu64 " definition, %" PRIu64 " split destination, %" PRIu64 " k0, %" PRIu64 " shared slot, %" PRIu64 " of %" PRIu64 " shape; %" PRIu64 " refused by the certificate alone, %" PRIu64 " by the verifier alone\n", tally.certificates, tally.oracle_certificates, tally.certificate_facts, tally.certificate_edits, tally.certificate_operands, tally.certificate_renames, tally.interference_refused, tally.order_refused, tally.definition_refused, tally.destination_refused, tally.k0_refused, tally.slot_refused, tally.shape_refused, tally.shape_mutants, tally.certificate_only, tally.verifier_only);
        printf("test-tier1-cfg: given dominator trees (#2611): %" PRIu64 " CFGs' anchors the same with and without an added entry, %" PRIu64 " blocks anchored at a latch\n", tally.given_trees, tally.given_latches);
        tier1_cfg_test_expect(&tally, tally.given_latches != 0, "some given tree's block takes a latch laid out before it (#2611)");
        printf("test-tier1-cfg: fixed sequences (#2767): %" PRIu64 " ranges avoid their registers, %" PRIu64 " readers' ranges across their own\n", tally.avoiding_ranges, tally.reading_ranges);
        printf("test-tier1-cfg: two problems in one call and in two: %" PRIu64 " cases, %" PRIu64 " preferred allocations taken, %" PRIu64 " refused, %" PRIu64 " freed of their definitions' registers; %" PRIu64 " with fixed ranges refused another column; %" PRIu64 " values' column sets probed, at most %u in one problem\n", tally.two_cases, tally.two_taken, tally.two_refused, tally.two_freed, tally.two_fixed, tally.column_values, tally.column_widest);
        result = tally.failures == 0 ? 0 : 1;
        arena_release(&work);
        arena_release(&scratch);
        arena_release(&output);
    }
    return result;
}

