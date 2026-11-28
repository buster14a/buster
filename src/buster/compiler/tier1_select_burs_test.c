// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The bottom-up rewrite selection's own checks (#141, tier1_select_burs.c), which test-tier1-select runs, and the
// per-function check the pipeline's tests and benchmark-tier1-select-source run on every function they plan:
//
// - the tables: built, the REG leaf's state first, every state's rules the rules' own derivations (a rule names its
//   nonterminal), every operator's table entry a state, and a node of each leaf operator its state;
// - the labeling and the rule choice against their equations (tier1_burs_test_equation) at every node of random
//   columns of every operator and child states drawn from every state the tables have, 16-lane tails and lone levels
//   among them;
// - per function (tier1_burs_check_function): each visible child the node's own earlier operand, every node's state
//   and rule their equations', every node of a class the grammar covers with a candidate of its own
//   (tier1_select_candidates), and the rule's candidate found for each, which it counts. The classes themselves the
//   plans' frozen digest checks (tier1_select_test_digest).
//
// Include after tier1_select_test.c. Keep the per-function check shared for the checker's code-certificate corpus;
// the backend unit alone reads the fixtures below it, at their original definition and inclusion points.

typedef struct Tier1BursCheck Tier1BursCheck;
struct Tier1BursCheck
{
    uint64_t functions;
    uint64_t nodes;
    // Nodes whose children, state or rule break their definitions.
    uint64_t mismatches;
    // Nodes of a class the grammar covers without a candidate: the class promised one.
    uint64_t uncovered;
    // Nodes of a class the grammar covers whose goal rule names a candidate the walks made, and those that took munch's.
    uint64_t matched;
    uint64_t fallbacks;
    // Integer comparisons classed as a truth value's link.
    uint64_t links;
    // Addresses and frame objects' addresses whose class was checked against its definition.
    uint64_t addresses;
};

// The plan's prerequisites, built here apart from it: each node's block, uses, last user, barriers, canonical answer and
// depth (a non-phi operand's depth plus one, as the plan orders its choices), and the nodes in levels. Returns whether
// the function is one the plan reads (operands in range and before their users within a block).
static bool tier1_burs_check_prepare(const IrFunction* function, const IrDominance* dominance, Arena* scratch, uint32_t* blocks, uint32_t* uses, uint32_t* users, uint32_t* barriers, uint8_t* canonical, uint16_t* known_bits,
                                     uint32_t* level_offsets, uint32_t* level_nodes, uint32_t* scope_workspace, uint32_t* maximum_depth)
{
    const uint32_t count = function->instruction_count;
    uint32_t* depths = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), _Alignof(uint32_t));
    bool valid = depths != 0;
    for (uint32_t block = 0; valid && block < function->block_count; block += 1)
    {
        for (uint32_t node = function->block_offsets[block]; node < function->block_offsets[block + 1]; node += 1)
        {
            blocks[node] = block;
        }
    }
    if (valid)
    {
        memset(uses, 0, (uint64_t)count * sizeof(uint32_t));
        memset(depths, 0, (uint64_t)count * sizeof(uint32_t));
        memset(level_offsets, 0, ((uint64_t)count + 2) * sizeof(uint32_t));
        memset(users, 0xff, (uint64_t)count * sizeof(uint32_t));
    }
    *maximum_depth = 0;
    for (uint32_t node = 0; valid && node < count; node += 1)
    {
        const uint32_t operand_count = (uint32_t)ir_operand_count(function, node);
        for (uint32_t p = 0; valid && p < operand_count; p += 1)
        {
            const IrOperand operand = ir_operand(function, node, p, blocks[node]);
            valid = operand.value < count;
            if (valid)
            {
                uses[operand.value] += 1;
                users[operand.value] = node;
                const uint32_t next = function->op[node] != IR_PHI && operand.value < node ? depths[operand.value] + 1 : 0;
                depths[node] = next > depths[node] ? next : depths[node];
            }
        }
        *maximum_depth = valid && depths[node] > *maximum_depth ? depths[node] : *maximum_depth;
    }
    for (uint32_t node = 0; valid && node < count; node += 1)
    {
        level_offsets[depths[node] + 1] += 1;
    }
    for (uint32_t level = 0; valid && level <= *maximum_depth; level += 1)
    {
        level_offsets[level + 1] += level_offsets[level];
    }
    // Placed in each level by node order, through a copy of the offsets.
    uint32_t* cursors = valid ? arena_push_array(scratch, (uint64_t)count + 2, sizeof(uint32_t), _Alignof(uint32_t)) : 0;
    valid = valid && cursors != 0;
    if (valid)
    {
        memcpy(cursors, level_offsets, ((uint64_t)count + 2) * sizeof(uint32_t));
        for (uint32_t node = 0; node < count; node += 1)
        {
            level_nodes[cursors[depths[node]]++] = node;
        }
        tier1_select_barriers(function, count, barriers);
        tier1_select_known_bits(function, dominance, blocks, cursors, known_bits);
        tier1_select_canonical(function, blocks, known_bits, canonical, cursors);
        tier1_select_scoped_ranges(function, dominance, blocks, known_bits, canonical, cursors, scope_workspace);
    }
    return valid;
}

// The labeling's and the rule choice's defining equations at `node`: its state is its operator's table entry at its
// visible children's states mapped (the REG leaf's state for an absent child), and its rule is its state's rule of its
// operator's goal. Whether both hold, from the columns as they are.
static bool tier1_burs_test_equation(uint32_t count, const uint32_t* classes, const uint32_t* kids, const uint32_t* states, const uint32_t* rules, uint32_t node)
{
    const uint32_t op = classes[node];
    const uint32_t left = kids[node] != BUSTER_IR_NONE ? states[kids[node]] : BUSTER_TIER1_BURS_REG_STATE;
    const uint32_t right = kids[count + node] != BUSTER_IR_NONE ? states[kids[count + node]] : BUSTER_TIER1_BURS_REG_STATE;
    const uint32_t row = tier1_burs_pool[tier1_burs_map_base[0][op] + left];
    const uint32_t column = tier1_burs_pool[tier1_burs_map_base[1][op] + right];
    const uint32_t state = tier1_burs_pool[tier1_burs_table_base[op] + row * tier1_burs_table_width[op] + column];
    return states[node] == state && rules[node] == tier1_burs_state_rules[states[node] * TIER1_BURS_NONTERMINALS + tier1_burs_goal[op]];
}

// The definition of an address's displacement fitting a disp32 (its constant index scaled, plus its offset, in exact
// arithmetic): the condition of its lea's candidate (tier1_select_candidates). An address past it has no candidate, and
// its class is the REG leaf's; tier1_burs_classify_vector's limbs must agree with it.
static bool tier1_burs_displacement(const IrFunction* function, uint32_t node)
{
    const IrAddress operand = function->addresses[function->c[node]];
    const uint32_t b = function->b[node];
    uint64_t constant = 0;
    const bool constant_index = b != BUSTER_IR_NONE && tier1_select_constant(function, b, &constant);
    int64_t displacement = 0;
    return !__builtin_mul_overflow((int64_t)(constant_index ? constant : 0), (int64_t)operand.scale, &displacement) && !__builtin_add_overflow(displacement, operand.offset, &displacement) &&
           displacement == (int64_t)(int32_t)displacement;
}

// Checks `function` (verified, with dominance) into `check`: its children, states and rules against their definitions,
// and the candidates of the grammar's classes.
static void tier1_burs_check_function(Tier1BursCheck* check, const IrFunction* function, const IrDominance* dominance, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = function->instruction_count;
    uint32_t* blocks = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* uses = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* users = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* barriers = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint8_t* canonical = arena_push_array(scratch, (uint64_t)count + 1, 1, 64);
    uint16_t* known_bits = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint16_t), 64);
    uint32_t* level_offsets = arena_push_array(scratch, (uint64_t)count + 2, sizeof(uint32_t), 64);
    uint32_t* level_nodes = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* words = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* columns[4];
    bool valid = blocks != 0 && uses != 0 && users != 0 && barriers != 0 && canonical != 0 && known_bits != 0 && level_offsets != 0 && level_nodes != 0 && words != 0 && tier1_select_initialize();
    // Classes, children (two columns), states and rules.
    columns[0] = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64) : 0;
    columns[1] = valid ? arena_push_array(scratch, (uint64_t)count * 2 + 1, sizeof(uint32_t), 64) : 0;
    columns[2] = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64) : 0;
    columns[3] = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64) : 0;
    valid = valid && columns[0] != 0 && columns[1] != 0 && columns[2] != 0 && columns[3] != 0;
    uint32_t maximum_depth = 0;
    valid = valid && tier1_burs_check_prepare(function, dominance, scratch, blocks, uses, users, barriers, canonical, known_bits, level_offsets, level_nodes, words, &maximum_depth);
    const Tier1SelectForest forest = {
        .function = function,
        .blocks = blocks,
        .uses = uses,
        .costs = 0,
        .barriers = barriers,
        .canonical = canonical,
        .known_bits = known_bits,
        .vectors = ir_function_has_vectors(function),
    };
    if (valid)
    {
        tier1_burs_classify_vector(&forest, users, words, columns[0], columns[1]);
        tier1_burs_label_vector(maximum_depth, level_offsets, level_nodes, count, columns[0], columns[1], columns[2]);
        tier1_burs_rules_vector(count, columns[0], columns[2], columns[3]);
        check->functions += 1;
        check->nodes += count;
    }
    // The candidates need the costs column the plan keeps: zero, which the candidates' costs alone read.
    uint32_t* costs = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64) : 0;
    valid = valid && costs != 0;
    if (valid)
    {
        memset(costs, 0, (uint64_t)count * sizeof(uint32_t));
    }
    const Tier1SelectForest costed = {
        .function = function,
        .blocks = blocks,
        .uses = uses,
        .costs = costs,
        .barriers = barriers,
        .canonical = canonical,
        .known_bits = known_bits,
        .vectors = ir_function_has_vectors(function),
    };
    for (uint32_t node = 0; valid && node < count; node += 1)
    {
        // A visible child is the node's own operand, an earlier node; its state and rule are their equations' (tier1_burs_test_equations).
        const uint32_t a = columns[1][node];
        const uint32_t b = columns[1][count + node];
        const bool same = (a == BUSTER_IR_NONE || (a == function->a[node] && a < node)) && (b == BUSTER_IR_NONE || (b == function->b[node] && b < node)) &&
                          tier1_burs_test_equation(count, columns[0], columns[1], columns[2], columns[3], node);
        if (!same && check->mismatches < 8)
        {
            fprintf(stderr, "  burs node %u (%s) breaks its equations: class %u, children %u,%u, state %u, rule %u\n", node, ir_op_names[function->op[node]],
                    columns[0][node], a, b, columns[2][node], columns[3][node]);
        }
        check->mismatches += !same;
        // An address's class by its displacement's definition (tier1_burs_displacement), and a frame object's address's by
        // its slot's (tier1_select_stack_object), which the vector classifier computes from gathers (#3450).
        const uint32_t op = function->op[node];
        if (op == IR_ADDRESS || op == IR_STACK_ADDR)
        {
            const uint32_t b_child = function->b[node];
            const uint32_t expected = op == IR_STACK_ADDR ? (tier1_select_stack_object(function, function->a[node]) ? (blocks[node] == 0 ? TIER1_BURS_OP_SLOT_X : TIER1_BURS_OP_SLOT) : TIER1_BURS_OP_REG) :
                                      !tier1_burs_displacement(function, node) ? TIER1_BURS_OP_REG :
                                      b_child == BUSTER_IR_NONE ? TIER1_BURS_OP_ADDR_NOIDX :
                                      b_child < count && function->op[b_child] == IR_CONST ? TIER1_BURS_OP_ADDR_CIDX : TIER1_BURS_OP_ADDR_IDX;
            if (columns[0][node] != expected && check->mismatches < 8)
            {
                fprintf(stderr, "  burs node %u (%s) has class %u, its definition's %u\n", node, ir_op_names[op], columns[0][node], expected);
            }
            check->mismatches += columns[0][node] != expected;
            check->addresses += 1;
        }
        // An integer comparison's class is the link's exactly where it is one (tier1_select_truth): eq or ne of the zero
        // extension two nodes before it, of the I1 right before that, and of a zero right before it that it alone uses,
        // in its block.
        const uint32_t class = columns[0][node];
        const uint32_t type = function->type[node];
        const bool plain_type = (type >= IR_I1 && type <= IR_I64) || type == IR_PTR || type == IR_F32 || type == IR_F64;
        const bool plain = op == IR_BR || op == IR_BR_INDIRECT || op == IR_SYMBOL_ADDR || op == IR_LIFETIME ||
                           op == IR_UNREACHABLE || op == IR_FENCE || op == IR_STACK_SAVE ||
                           (op == IR_PARAM && plain_type && function->a[node] < BUSTER_TIER1_SELECT_MAX_ARGUMENTS) ||
                           (op == IR_PHI && plain_type) || (op == IR_COPY && plain_type && type != IR_I1) ||
                           ((op == IR_NEG || op == IR_NOT) && (type == IR_I32 || type == IR_I64)) ||
                           (op == IR_TRUNC && (type == IR_I8 || type == IR_I16 || type == IR_I32) && function->type[function->a[node]] == IR_I64) ||
                           (op == IR_SEXT && (type == IR_I16 || type == IR_I32 || type == IR_I64) && function->op[function->a[node]] == IR_PARAM &&
                            (function->type[function->a[node]] >= IR_I8 && function->type[function->a[node]] <= IR_I32)) || (op == IR_RET && function->a[node] == BUSTER_IR_NONE);
        if (plain)
        {
            check->mismatches += class != TIER1_BURS_OP_DIRECT;
        }
        if (class == TIER1_BURS_OP_ICMP || class == TIER1_BURS_OP_ICMP_LINK)
        {
            const uint32_t extended = function->a[node];
            const uint32_t zero = function->b[node];
            const bool shaped = function->c[node] <= IR_NE && extended + 2 == node && zero + 1 == node && function->op[extended] == IR_ZEXT && function->op[zero] == IR_CONST;
            const bool link = shaped && function->a[extended] + 1 == extended && function->type[function->a[extended]] == IR_I1 && function->constants[function->a[zero]].low == 0 &&
                              uses[zero] == 1 && blocks[zero] == blocks[node];
            if ((class == TIER1_BURS_OP_ICMP_LINK) != link && check->mismatches < 8)
            {
                fprintf(stderr, "  burs comparison %u classed %u where its link's definition says %s\n", node, class, link ? "a link" : "none");
            }
            check->mismatches += (class == TIER1_BURS_OP_ICMP_LINK) != link;
            check->links += class == TIER1_BURS_OP_ICMP_LINK;
        }
        if (columns[0][node] != TIER1_BURS_OP_REG)
        {
            const Tier1SelectCandidates candidates = tier1_select_candidates(&costed, node, TIER1_RULE_NONE);
            bool munched = false;
            tier1_burs_pick(&candidates, columns[3][node], &munched);
            if (class >= TIER1_BURS_OP_CALL && class <= TIER1_BURS_OP_CALL_ROW)
            {
                const Tier1BursRule* rule = &tier1_burs_rules[columns[3][node]];
                check->mismatches += candidates.count != 1 || munched || rule->action != TIER1_BURS_ACTION_PLAIN ||
                                     rule->kind != candidates.patterns[0].rule;
            }
            if (class == TIER1_BURS_OP_DIRECT)
            {
                // A second pattern requires a grammar choice. Catch it here even if both emit the same code today.
                check->mismatches += candidates.count != 1 || munched || tier1_burs_rules[columns[3][node]].action != TIER1_BURS_ACTION_PLAIN;
            }
            if (candidates.count == 0 && check->uncovered < 8)
            {
                fprintf(stderr, "  burs class %u without a candidate at node %u (%s, type %u, a %u, b %u, uses %u)\n", columns[0][node], node, ir_op_names[function->op[node]], function->type[node],
                        function->a[node], function->b[node], uses[node]);
            }
            check->uncovered += candidates.count == 0;
            check->matched += candidates.count != 0 && !munched;
            check->fallbacks += munched;
        }
    }
    arena_pop_to(scratch, mark);
}

#if BUSTER_IN_UNIT(BUSTER_UNIT_BACKEND)
// The most representers of any operator's child position: the largest a map's value is, plus one.
static uint32_t tier1_burs_test_representers(void)
{
    uint32_t most = 0;
    for (uint32_t op = 0; op < TIER1_BURS_OPERATORS; op += 1)
    {
        for (uint32_t position = 0; position < tier1_burs_arity[op] && position < 2; position += 1)
        {
            for (uint32_t s = 0; s < tier1_burs_state_count; s += 1)
            {
                const uint32_t representer = tier1_burs_pool[tier1_burs_map_base[position][op] + s];
                most = representer + 1 > most ? representer + 1 : most;
            }
        }
    }
    return most;
}

// The tables' invariants, and the labeling and rule kernels against their equations on random columns.
static void tier1_select_test_burs_tables(Tier1SelectTest* test)
{
    tier1_select_test_expect(test, tier1_select_initialize() && tier1_select_initialized(), "the selection's state tables build");
    tier1_select_test_expect(test, tier1_burs_state_count > 1 && tier1_burs_state_count <= BUSTER_TIER1_BURS_MAX_STATES && tier1_burs_rule_count <= 256, "the tables have states and rules within their bounds");
    tier1_select_test_expect(test, tier1_burs_pool_length > 0 && tier1_burs_pool_length <= BUSTER_TIER1_BURS_POOL && tier1_burs_pool_length <= INT32_MAX / 2,
                             "the generated pool fits its storage and signed gather offsets");
    // The REG leaf's state is the first: it derives a register (its rule munch's), and from it an address and an index.
    const uint8_t* reg = &tier1_burs_state_rules[BUSTER_TIER1_BURS_REG_STATE * TIER1_BURS_NONTERMINALS];
    tier1_select_test_expect(test, tier1_burs_rules[reg[TIER1_BURS_REG]].action == TIER1_BURS_ACTION_MUNCH && reg[TIER1_BURS_A1] != 0 && reg[TIER1_BURS_INDEX] != 0 && reg[TIER1_BURS_IMM] == 0,
                             "the REG leaf's state derives a register, an address and an index, and no immediate");
    // Every state's rule per nonterminal derives that nonterminal; every leaf operator's table is a state.
    bool rules = true;
    for (uint32_t s = 0; s < tier1_burs_state_count; s += 1)
    {
        for (uint32_t k = 0; k < TIER1_BURS_NONTERMINALS; k += 1)
        {
            const uint32_t rule = tier1_burs_state_rules[s * TIER1_BURS_NONTERMINALS + k];
            rules &= rule < tier1_burs_rule_count && (rule == 0 || tier1_burs_rules[rule].lhs == k);
        }
    }
    tier1_select_test_expect(test, rules, "every state's rule of a nonterminal derives it");
    bool entries = true;
    for (uint32_t op = 0; op < TIER1_BURS_OPERATORS; op += 1)
    {
        const uint32_t rows = tier1_burs_arity[op] == 0 ? 1 : UINT32_MAX;
        entries &= tier1_burs_table_base[op] < tier1_burs_pool_length && tier1_burs_map_base[0][op] < tier1_burs_pool_length && tier1_burs_map_base[1][op] < tier1_burs_pool_length;
        entries &= rows != 1 || tier1_burs_pool[tier1_burs_table_base[op]] < tier1_burs_state_count;
        for (uint32_t s = 0; s < tier1_burs_state_count; s += 1)
        {
            const uint32_t row = tier1_burs_pool[tier1_burs_map_base[0][op] + s];
            const uint32_t column = tier1_burs_pool[tier1_burs_map_base[1][op] + s];
            entries &= tier1_burs_pool[tier1_burs_table_base[op] + row * tier1_burs_table_width[op] + column % tier1_burs_table_width[op]] < tier1_burs_state_count;
        }
    }
    tier1_select_test_expect(test, entries, "every operator's maps and table entries are in the pool and are states");
    // A constant's state derives its class's nonterminals and no others (the class bits' bijection).
    bool constants = true;
    static const uint8_t constant_nonterminals[7] = { TIER1_BURS_IMM, TIER1_BURS_SCALE, TIER1_BURS_POW2, TIER1_BURS_M359, TIER1_BURS_SHAMT, TIER1_BURS_ZERO, TIER1_BURS_DISP };
    for (uint32_t bits = 0; bits < BUSTER_TIER1_BURS_CONSTANT_CLASSES; bits += 1)
    {
        const uint32_t state = tier1_burs_pool[tier1_burs_table_base[TIER1_BURS_OP_CONST + bits]];
        for (uint32_t k = 0; k < 7; k += 1)
        {
            constants &= (tier1_burs_state_rules[state * TIER1_BURS_NONTERMINALS + constant_nonterminals[k]] != 0) == (((bits >> k) & 1) != 0);
        }
        constants &= tier1_burs_rules[tier1_burs_state_rules[state * TIER1_BURS_NONTERMINALS + TIER1_BURS_REG]].kind == TIER1_RULE_CONSTANT;
    }
    tier1_select_test_expect(test, constants, "each constant class derives exactly its nonterminals, and its move");
    // Random columns: nodes of random operators whose children are earlier nodes or none, in levels by their depth.
    uint64_t random = UINT64_C(0x9e3779b97f4a7c15);
    for (uint32_t round = 0; round < 64; round += 1)
    {
        const uint32_t count = 1 + (uint32_t)(tier1_select_test_random(&random) % (round < 8 ? 40 : 700));
        uint32_t* columns = malloc((uint64_t)count * 16 * sizeof(uint32_t) + 256);
        tier1_select_test_expect(test, columns != 0, "random columns' memory");
        if (columns == 0)
        {
            break;
        }
        uint32_t* classes = columns;
        uint32_t* kids = classes + count;
        uint32_t* depths = kids + 2 * (uint64_t)count;
        uint32_t* level_offsets = depths + count;
        uint32_t* level_nodes = level_offsets + count + 2;
        uint32_t* states[2] = { level_nodes + count, level_nodes + 2 * (uint64_t)count };
        uint32_t* rules[2] = { level_nodes + 3 * (uint64_t)count, level_nodes + 4 * (uint64_t)count };
        uint32_t maximum = 0;
        memset(level_offsets, 0, ((uint64_t)count + 2) * sizeof(uint32_t));
        for (uint32_t node = 0; node < count; node += 1)
        {
            classes[node] = (uint32_t)(tier1_select_test_random(&random) % TIER1_BURS_OPERATORS);
            depths[node] = 0;
            for (uint32_t position = 0; position < 2; position += 1)
            {
                const bool child = node != 0 && position < tier1_burs_arity[classes[node]] && tier1_select_test_random(&random) % 4 != 0;
                const uint32_t kid = child ? (uint32_t)(tier1_select_test_random(&random) % node) : BUSTER_IR_NONE;
                kids[(uint64_t)position * count + node] = kid;
                depths[node] = child && depths[kid] + 1 > depths[node] ? depths[kid] + 1 : depths[node];
            }
            maximum = depths[node] > maximum ? depths[node] : maximum;
            level_offsets[depths[node] + 1] += 1;
        }
        for (uint32_t level = 0; level <= maximum; level += 1)
        {
            level_offsets[level + 1] += level_offsets[level];
        }
        uint32_t* cursors = level_nodes + 5 * (uint64_t)count;
        memcpy(cursors, level_offsets, ((uint64_t)count + 2) * sizeof(uint32_t));
        for (uint32_t node = 0; node < count; node += 1)
        {
            level_nodes[cursors[depths[node]]++] = node;
        }
        memset(states[0], 0xa5, (uint64_t)count * sizeof(uint32_t));
        memset(rules[0], 0xa5, (uint64_t)count * sizeof(uint32_t));
        tier1_burs_label_vector(maximum, level_offsets, level_nodes, count, classes, kids, states[0]);
        tier1_burs_rules_vector(count, classes, states[0], rules[0]);
        uint32_t broken = 0;
        for (uint32_t node = 0; node < count; node += 1)
        {
            broken += !tier1_burs_test_equation(count, classes, kids, states[0], rules[0], node);
        }
        tier1_select_test_expect(test, broken == 0, "every node's state and rule on random columns are their equations' over its children's states");
        free(columns);
    }
}

// The IR's text fixtures (tests/ir and tests/ir/optimize), parsed, verified and checked (tier1_burs_check_function).
static void tier1_select_test_burs_fixtures(Tier1SelectTest* test)
{
    static const char* const directories[] = { "tests/ir", "tests/ir/optimize" };
    Arena source = arena_reserve(UINT64_C(1) << 24);
    Arena input = arena_reserve(UINT64_C(1) << 28);
    Arena scratch = arena_reserve(UINT64_C(1) << 30);
    Tier1BursCheck check = { 0 };
    uint32_t files = 0;
    for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(directories); d += 1)
    {
        DIR* directory = opendir(directories[d]);
        struct dirent* entry = directory != 0 ? readdir(directory) : 0;
        while (entry != 0)
        {
            const uint64_t length = strlen(entry->d_name);
            if (length > 3 && strcmp(entry->d_name + length - 3, ".ir") == 0)
            {
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", directories[d], entry->d_name);
                arena_pop_to(&source, 0);
                arena_pop_to(&input, 0);
                String8 text = { 0 };
                IrFunction function = { 0 };
                if (ir_parse_test_read(path, &source, &text) && ir_parse(text, &input, &scratch, &function).error == IR_PARSE_OK && ir_verify(&function, &scratch, 0).error == IR_OK)
                {
                    tier1_burs_check_function(&check, &function, 0, &scratch);
                    files += 1;
                }
            }
            entry = readdir(directory);
        }
        if (directory != 0)
        {
            closedir(directory);
        }
    }
    tier1_select_test_expect(test, files != 0 && check.mismatches == 0 && check.uncovered == 0, "the IR fixtures' children, states and rules hold their definitions, every covered class with a candidate");
    // Every covered node's rule names a candidate the walks made, so that a class the grammar gives and the candidates'
    // own conditions agree (a floating == or !='s, #2922, both asking the same select's recurrence): no munch's fallback.
    tier1_select_test_expect(test, check.fallbacks == 0, "the IR fixtures' rules name candidates the walks made, with no munch's fallback");

    // As the selection's own summary: on standard error where standard output carries the manifest (--manifest).
    fprintf(test->manifest ? stderr : stdout, "tier1 bottom-up rewrite: %u states, %u rules, at most %u representers in a position, a pool of %u entries; the IR fixtures: %" PRIu64 " functions, %" PRIu64 " nodes, %" PRIu64 " rules' candidates, %" PRIu64 " munch's, %" PRIu64
           " mismatches, %" PRIu64 " uncovered\n",
           tier1_burs_state_count, tier1_burs_rule_count, tier1_burs_test_representers(), tier1_burs_pool_length, check.functions, check.nodes, check.matched, check.fallbacks, check.mismatches, check.uncovered);
    arena_release(&scratch);
    arena_release(&input);
    arena_release(&source);
}

// Addresses around a disp32's edges (tier1_burs_displacement): constant indices and offsets inside and past 32 bits, at
// every scale, among them pairs past it whose sum fits, which the vector classifier decides by exact 64-bit arithmetic; an index
// that isn't a constant and none, with offsets on both sides of the edge. The definitions must hold, every address of a covered
// class must have its candidate, and both classes must occur.
static void tier1_select_test_burs_addresses(Tier1SelectTest* test)
{
    // Past 2^60 every scale's product meets 64 bits' edge: exactly 2^63 overflows, exactly -2^63 doesn't.
    static const int64_t values[] = { 0, 1, -1, 7, INT32_MAX, INT32_MIN, (int64_t)INT32_MAX + 1, (int64_t)INT32_MIN - 1, INT64_C(1) << 28, -(INT64_C(1) << 28), INT64_C(1) << 40, -(INT64_C(1) << 40),
                                      INT64_C(1) << 60, -(INT64_C(1) << 60), -(INT64_C(1) << 60) - 1, INT64_C(1) << 61, -(INT64_C(1) << 61), -(INT64_C(1) << 61) - 1, INT64_C(1) << 62, -(INT64_C(1) << 62),
                                      -(INT64_C(1) << 62) - 1, INT64_MAX, INT64_MIN };
    static const int64_t offsets[] = { 0, 16, -16, INT32_MAX, INT32_MIN, (int64_t)INT32_MAX + 1, (int64_t)INT32_MIN - 1, -(INT64_C(1) << 40), INT64_C(1) << 40, -(INT64_C(1) << 31) + 8, INT64_MAX, INT64_MIN };
    static const uint32_t scales[] = { 1, 2, 4, 8 };
    const uint32_t value_count = BUSTER_ARRAY_LENGTH(values);
    Arena text = arena_reserve(UINT64_C(1) << 24);
    Arena input = arena_reserve(UINT64_C(1) << 26);
    Arena scratch = arena_reserve(UINT64_C(1) << 28);
    char* source = text.base != 0 ? arena_push_array(&text, UINT64_C(1) << 22, 1, 1) : 0;
    uint64_t length = 0;
    const uint64_t room = UINT64_C(1) << 22;
    bool valid = source != 0 && input.base != 0 && scratch.base != 0;
    if (valid)
    {
        length += (uint64_t)snprintf(source + length, room - length, "function (ptr, i64) -> i64 {\nb0:\n  %%0 = param.ptr 0\n  %%1 = param.i64 1\n");
        for (uint32_t v = 0; v < value_count; v += 1)
        {
            length += (uint64_t)snprintf(source + length, room - length, "  %%%u = const.i64 0x0000000000000000%016" PRIx64 "\n", v + 2, (uint64_t)values[v]);
        }
        uint32_t next = value_count + 2;
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(offsets); o += 1)
        {
            for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(scales); k += 1)
            {
                for (uint32_t v = 0; v < value_count; v += 1)
                {
                    length += (uint64_t)snprintf(source + length, room - length, "  %%%u = address.ptr %%0 + %%%u * %u offset=%" PRId64 "\n", next, v + 2, scales[k], offsets[o]);
                    next += 1;
                }
                length += (uint64_t)snprintf(source + length, room - length, "  %%%u = address.ptr %%0 + %%1 * %u offset=%" PRId64 "\n", next, scales[k], offsets[o]);
                next += 1;
            }
            // An address without an index, loaded, so that a load reads it as its child.
            length += (uint64_t)snprintf(source + length, room - length, "  %%%u = address.ptr %%0 offset=%" PRId64 "\n  %%%u = load.i64 %%%u align=8\n", next, offsets[o], next + 1, next);
            next += 2;
        }
        length += (uint64_t)snprintf(source + length, room - length, "  %%%u = ret.void %%1\n}\n", next);
        valid = length < room;
    }
    IrFunction function = { 0 };
    valid = valid && ir_parse((String8){ .pointer = source, .length = length }, &input, &scratch, &function).error == IR_PARSE_OK && ir_verify(&function, &scratch, 0).error == IR_OK;
    tier1_select_test_expect(test, valid, "the addresses' fixture parses and verifies");
    Tier1BursCheck check = { 0 };
    uint32_t covered = 0;
    uint32_t past = 0;
    if (valid)
    {
        tier1_burs_check_function(&check, &function, 0, &scratch);
        for (uint32_t node = 0; node < function.instruction_count; node += 1)
        {
            const bool address = function.op[node] == IR_ADDRESS;
            const bool fits = address && tier1_burs_displacement(&function, node);
            covered += fits;
            past += address && !fits;
        }
    }
    tier1_select_test_expect(test, check.functions == 1 && check.mismatches == 0 && check.uncovered == 0 && covered != 0 && past != 0,
                             "addresses around a disp32's edges: the definitions hold, a candidate for each covered one, both classes");
    fprintf(test->manifest ? stderr : stdout, "tier1 bottom-up rewrite addresses: %u covered, %u past a disp32\n", covered, past);
    arena_release(&scratch);
    arena_release(&input);
    arena_release(&text);
}

// Frame objects' addresses at every lane of the classifier's 16, in the entry block and another: slots of each kind
// tier1_select_stack_object tells apart (an object, the variadic overflow area, a size past the planner's bound, an
// alignment past it), so that each class (SLOT_X, SLOT, REG) occurs; the per-function check compares each with the
// definition.
// `slot_count` slots (at least 4): the classifier looks the first 64 up in a register and gathers past them.
static void tier1_select_test_burs_slots(Tier1SelectTest* test, uint32_t slot_count)
{
    Arena text = arena_reserve(UINT64_C(1) << 20);
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    const uint64_t room = UINT64_C(1) << 16;
    char* source = text.base != 0 ? arena_push_array(&text, room, 1, 1) : 0;
    uint64_t length = 0;
    bool valid = source != 0 && input.base != 0 && scratch.base != 0;
    if (valid)
    {
        // A variadic definition (tests/ir/aggregates.ir's variadic line), whose one overflow area is slot 0; the slots
        // after it an object, a size past the bound and an alignment past it in turn; the last two at the bounds.
        length += (uint64_t)snprintf(source + length, room - length,
                                     "function (ptr, i64) -> ptr {\n  variadic call(8,4,2,0,ret(1,0,8,0,2,255,255))\n  slot $0 size=0 align=16 type=void flags=0x11\n");
        for (uint32_t k = 1; k + 2 < slot_count; k += 1)
        {
            const uint32_t kind = k % 3;
            length += (uint64_t)snprintf(source + length, room - length, "  slot $%u size=%" PRIu64 " align=%u type=void flags=0x01\n", k,
                                         kind == 1 ? BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES + 8 : (uint64_t)16, kind == 2 ? BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT * 2u : 8u);
        }
        // The bounds themselves, which are objects: exactly the largest size and exactly the largest alignment.
        length += (uint64_t)snprintf(source + length, room - length, "  slot $%u size=%" PRIu64 " align=8 type=void flags=0x01\n  slot $%u size=16 align=%u type=void flags=0x01\n",
                                     slot_count - 2, BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES, slot_count - 1, BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT);
        length += (uint64_t)snprintf(source + length, room - length, "b0:\n  %%0 = param.ptr 0\n  %%1 = param.i64 1\n");
        for (uint32_t k = 0; k < slot_count; k += 1)
        {
            length += (uint64_t)snprintf(source + length, room - length, "  %%%u = stack_addr.ptr $%u\n", k + 2, k);
        }
        length += (uint64_t)snprintf(source + length, room - length, "  %%%u = br.void b1\nb1:\n", slot_count + 2);
        for (uint32_t k = 0; k < slot_count; k += 1)
        {
            length += (uint64_t)snprintf(source + length, room - length, "  %%%u = stack_addr.ptr $%u\n", k + slot_count + 3, k);
        }
        length += (uint64_t)snprintf(source + length, room - length, "  %%%u = ret.void %%0\n}\n", 2 * slot_count + 3);
        valid = length < room;
    }
    IrFunction function = { 0 };
    valid = valid && ir_parse((String8){ .pointer = source, .length = length }, &input, &scratch, &function).error == IR_PARSE_OK && ir_verify(&function, &scratch, 0).error == IR_OK;
    tier1_select_test_expect(test, valid, "the frame objects' fixture parses and verifies");
    Tier1BursCheck check = { 0 };
    uint32_t objects = 0;
    uint32_t others = 0;
    if (valid)
    {
        tier1_burs_check_function(&check, &function, 0, &scratch);
        for (uint32_t node = 0; node < function.instruction_count; node += 1)
        {
            objects += function.op[node] == IR_STACK_ADDR && tier1_select_stack_object(&function, function.a[node]);
            others += function.op[node] == IR_STACK_ADDR && !tier1_select_stack_object(&function, function.a[node]);
        }
    }
    // Objects: every third of slots 1 to slot_count - 3, and the two at the bounds; in both blocks.
    const uint32_t object_slots = (slot_count - 3) / 3 + 2;
    tier1_select_test_expect(test, check.functions == 1 && check.mismatches == 0 && check.addresses == 2 * slot_count && objects == 2 * object_slots && others == 2 * (slot_count - object_slots),
                             "frame objects' addresses: every class its slot's definition's, in both blocks, below 64 slots and past them");
    arena_release(&scratch);
    arena_release(&input);
    arena_release(&text);
}

// The exact edges, with their classes written out (#3493's review): a constant times 8 that is exactly 2^63 (overflows:
// REG) and one just past -2^63 (REG), and frame objects of exactly the largest size and alignment (SLOT_X).
static void tier1_select_test_burs_edges(Tier1SelectTest* test)
{
    static const char source[] = "function (ptr, i64) -> i64 {\n"
                                 "  slot $0 size=268435456 align=8 type=void flags=0x01\n"
                                 "  slot $1 size=16 align=1073741824 type=void flags=0x01\n"
                                 "b0:\n"
                                 "  %0 = param.ptr 0\n"
                                 "  %1 = param.i64 1\n"
                                 "  %2 = const.i64 0x00000000000000001000000000000000\n"
                                 "  %3 = const.i64 0x0000000000000000efffffffffffffff\n"
                                 "  %4 = address.ptr %0 + %2 * 8 offset=9223372036854775807\n"
                                 "  %5 = address.ptr %0 + %3 * 8 offset=-9223372036854775808\n"
                                 "  %6 = stack_addr.ptr $0\n"
                                 "  %7 = stack_addr.ptr $1\n"
                                 "  %8 = ret.void %1\n"
                                 "}\n";
    _Static_assert(BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES == 268435456 && BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT == 1073741824, "the edges' fixture names the planner's bounds");
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    IrFunction function = { 0 };
    bool valid = input.base != 0 && scratch.base != 0 && ir_parse((String8){ .pointer = (char*)source, .length = sizeof(source) - 1 }, &input, &scratch, &function).error == IR_PARSE_OK &&
                 ir_verify(&function, &scratch, 0).error == IR_OK;
    tier1_select_test_expect(test, valid, "the edges' fixture parses and verifies");
    Tier1BursCheck check = { 0 };
    if (valid)
    {
        tier1_burs_check_function(&check, &function, 0, &scratch);
        valid = !tier1_burs_displacement(&function, 4) && !tier1_burs_displacement(&function, 5) && tier1_select_stack_object(&function, 0) && tier1_select_stack_object(&function, 1);
    }
    tier1_select_test_expect(test, valid && check.mismatches == 0 && check.addresses == 4, "a product of exactly 2^63, one past -2^63, and the planner's exact bounds: their definitions' classes");
    arena_release(&scratch);
    arena_release(&input);
}

// Truth values' links at every lane of the classifier's 16: 32 chains of five nodes, (a < b) zero-extended, compared
// eq or ne with a zero (a one in the last 16, which are no links), then an xor of the two comparisons. A chain's link
// sits at node 5k + 5, so the first 16 put one in each lane. Exactly those 16 must be classed links.
static void tier1_select_test_burs_links(Tier1SelectTest* test)
{
    Arena text = arena_reserve(UINT64_C(1) << 20);
    Arena input = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    const uint64_t room = UINT64_C(1) << 16;
    char* source = text.base != 0 ? arena_push_array(&text, room, 1, 1) : 0;
    uint64_t length = 0;
    bool valid = source != 0 && input.base != 0 && scratch.base != 0;
    if (valid)
    {
        length += (uint64_t)snprintf(source + length, room - length, "function (i64, i64) -> i64 {\nb0:\n  %%0 = param.i64 0\n  %%1 = param.i64 1\n");
        for (uint32_t k = 0; k < 32; k += 1)
        {
            const uint32_t n = 2 + 5 * k;
            length += (uint64_t)snprintf(source + length, room - length,
                                         "  %%%u = icmp.i1 %%0, %%1 lt\n  %%%u = zext.i32 %%%u\n  %%%u = const.i32 0x0000000000000000000000000000000%u\n  %%%u = icmp.i1 %%%u, %%%u %s\n  %%%u = xor.i1 %%%u, %%%u\n",
                                         n, n + 1, n, n + 2, k < 16 ? 0 : 1, n + 3, n + 1, n + 2, (k & 1) != 0 ? "ne" : "eq", n + 4, n + 3, n);
        }
        length += (uint64_t)snprintf(source + length, room - length, "  %%%u = ret.void %%1\n}\n", 2 + 5 * 32);
        valid = length < room;
    }
    IrFunction function = { 0 };
    valid = valid && ir_parse((String8){ .pointer = source, .length = length }, &input, &scratch, &function).error == IR_PARSE_OK && ir_verify(&function, &scratch, 0).error == IR_OK;
    tier1_select_test_expect(test, valid, "the links' fixture parses and verifies");
    Tier1BursCheck check = { 0 };
    if (valid)
    {
        tier1_burs_check_function(&check, &function, 0, &scratch);
    }
    tier1_select_test_expect(test, check.functions == 1 && check.mismatches == 0 && check.uncovered == 0 && check.links == 16,
                             "a truth value's link is classed one in every lane, and a comparison with a one is not");
    arena_release(&scratch);
    arena_release(&input);
    arena_release(&text);
}

static void tier1_select_test_burs(Tier1SelectTest* test)
{
    tier1_select_test_burs_tables(test);
    tier1_select_test_burs_fixtures(test);
    tier1_select_test_burs_addresses(test);
    static const uint32_t slot_counts[] = { 23, 32, 33, 63, 64, 65, 100 };
    for (uint32_t s = 0; s < sizeof(slot_counts) / sizeof(slot_counts[0]); s += 1)
    {
        tier1_select_test_burs_slots(test, slot_counts[s]);
    }
    tier1_select_test_burs_edges(test);
    tier1_select_test_burs_links(test);
}
#endif
