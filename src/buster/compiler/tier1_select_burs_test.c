// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The bottom-up rewrite selection's own checks (#141, tier1_select_burs.c), which test-tier1-select runs, and the
// per-function twin check the pipeline's tests and benchmark-tier1-select-source run on every function they plan:
//
// - the tables: built, the REG leaf's state first, every state's rules the rules' own derivations (a rule names its
//   nonterminal), every operator's table entry a state, and a node of each leaf operator its state;
// - the labeling and the rule choice, vector path against scalar twin, on random columns of every operator and child
//   states drawn from every state the tables have, 16-lane tails and lone levels among them;
// - per function (tier1_burs_check_function): the classes and visible children of the vector classifier against the
//   scalar one, the states and the rules of both kernels, every node of a class the grammar covers with a candidate of
//   its own (tier1_select_candidates), and the rule's candidate found for each, which it counts.
//
// Include after tier1_select_test.c.

typedef struct Tier1BursCheck Tier1BursCheck;
struct Tier1BursCheck
{
    uint64_t functions;
    uint64_t nodes;
    // Nodes whose class, children, state or rule differs between the twins.
    uint64_t mismatches;
    // Nodes of a class the grammar covers without a candidate: the class promised one.
    uint64_t uncovered;
    // Nodes of a class the grammar covers whose goal rule names a candidate the walks made, and those that took munch's.
    uint64_t matched;
    uint64_t fallbacks;
};

// The plan's prerequisites, built here apart from it: each node's block, uses, last user, barriers, canonical answer and
// depth (a non-phi operand's depth plus one, as the plan orders its choices), and the nodes in levels. Returns whether
// the function is one the plan reads (operands in range and before their users within a block).
static bool tier1_burs_check_prepare(const IrFunction* function, Arena* scratch, uint32_t* blocks, uint32_t* uses, uint32_t* users, uint32_t* barriers, uint8_t* canonical,
                                     uint32_t* level_offsets, uint32_t* level_nodes, uint32_t* maximum_depth)
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
        tier1_select_barriers_scalar(function, count, barriers);
        tier1_select_canonical(function, blocks, canonical);
    }
    return valid;
}

// Checks `function` (verified, with dominance) into `check`: the twins' classes, children, states and rules, and the
// candidates of the grammar's classes.
static void tier1_burs_check_function(Tier1BursCheck* check, const IrFunction* function, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = function->instruction_count;
    uint32_t* blocks = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* uses = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* users = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* barriers = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint8_t* canonical = arena_push_array(scratch, (uint64_t)count + 1, 1, 64);
    uint32_t* level_offsets = arena_push_array(scratch, (uint64_t)count + 2, sizeof(uint32_t), 64);
    uint32_t* level_nodes = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* words = arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64);
    uint32_t* columns[2][4];
    bool valid = blocks != 0 && uses != 0 && users != 0 && barriers != 0 && canonical != 0 && level_offsets != 0 && level_nodes != 0 && words != 0 && tier1_select_initialize();
    for (uint32_t path = 0; path < 2; path += 1)
    {
        // Classes, children (two columns), states and rules.
        columns[path][0] = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64) : 0;
        columns[path][1] = valid ? arena_push_array(scratch, (uint64_t)count * 2 + 1, sizeof(uint32_t), 64) : 0;
        columns[path][2] = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64) : 0;
        columns[path][3] = valid ? arena_push_array(scratch, (uint64_t)count + 1, sizeof(uint32_t), 64) : 0;
        valid = valid && columns[path][0] != 0 && columns[path][1] != 0 && columns[path][2] != 0 && columns[path][3] != 0;
    }
    uint32_t maximum_depth = 0;
    valid = valid && tier1_burs_check_prepare(function, scratch, blocks, uses, users, barriers, canonical, level_offsets, level_nodes, &maximum_depth);
    const Tier1SelectForest forest = {
        .function = function,
        .blocks = blocks,
        .uses = uses,
        .costs = 0,
        .barriers = barriers,
        .canonical = canonical,
    };
    if (valid)
    {
        tier1_burs_classify_scalar(&forest, users, columns[1][0], columns[1][1]);
        tier1_burs_label_scalar(count, columns[1][0], columns[1][1], columns[1][2]);
        tier1_burs_rules_scalar(count, columns[1][0], columns[1][2], columns[1][3]);
#if BUSTER_HOST_VECTOR
        tier1_burs_classify_vector(&forest, users, words, columns[0][0], columns[0][1]);
        tier1_burs_label_vector(maximum_depth, level_offsets, level_nodes, count, columns[0][0], columns[0][1], columns[0][2]);
        tier1_burs_rules_vector(count, columns[0][0], columns[0][2], columns[0][3]);
#else
        for (uint32_t k = 0; k < 4; k += 1)
        {
            memcpy(columns[0][k], columns[1][k], (uint64_t)count * (k == 1 ? 2 : 1) * sizeof(uint32_t));
        }
#endif
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
    };
    for (uint32_t node = 0; valid && node < count; node += 1)
    {
        const bool same = columns[0][0][node] == columns[1][0][node] && columns[0][1][node] == columns[1][1][node] && columns[0][1][count + node] == columns[1][1][count + node] &&
                          columns[0][2][node] == columns[1][2][node] && columns[0][3][node] == columns[1][3][node];
        if (!same && check->mismatches < 8)
        {
            fprintf(stderr, "  burs twins differ at node %u (%s): class %u/%u, children %u,%u/%u,%u, state %u/%u, rule %u/%u\n", node, ir_op_names[function->op[node]],
                    columns[0][0][node], columns[1][0][node], columns[0][1][node], columns[0][1][count + node], columns[1][1][node], columns[1][1][count + node],
                    columns[0][2][node], columns[1][2][node], columns[0][3][node], columns[1][3][node]);
        }
        check->mismatches += !same;
        if (columns[1][0][node] != TIER1_BURS_OP_REG)
        {
            const Tier1SelectCandidates candidates = tier1_select_candidates(&costed, node);
            bool munched = false;
            tier1_burs_pick(&candidates, columns[1][3][node], &munched);
            if (candidates.count == 0 && check->uncovered < 8)
            {
                fprintf(stderr, "  burs class %u without a candidate at node %u (%s, type %u, a %u, b %u, uses %u)\n", columns[1][0][node], node, ir_op_names[function->op[node]], function->type[node],
                        function->a[node], function->b[node], uses[node]);
            }
            check->uncovered += candidates.count == 0;
            check->matched += candidates.count != 0 && !munched;
            check->fallbacks += munched;
        }
    }
    arena_pop_to(scratch, mark);
}

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

// The tables' invariants, and the labeling and rule kernels against their twins on random columns.
static void tier1_select_test_burs_tables(Tier1SelectTest* test)
{
    tier1_select_test_expect(test, tier1_select_initialize() && tier1_select_initialized(), "the selection's state tables build");
    tier1_select_test_expect(test, tier1_burs_state_count > 1 && tier1_burs_state_count <= BUSTER_TIER1_BURS_MAX_STATES && tier1_burs_rule_count <= 256, "the tables have states and rules within their bounds");
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
        tier1_burs_label_scalar(count, classes, kids, states[1]);
        tier1_burs_rules_scalar(count, classes, states[1], rules[1]);
#if BUSTER_HOST_VECTOR
        tier1_burs_label_vector(maximum, level_offsets, level_nodes, count, classes, kids, states[0]);
        tier1_burs_rules_vector(count, classes, states[0], rules[0]);
#else
        memcpy(states[0], states[1], (uint64_t)count * sizeof(uint32_t));
        memcpy(rules[0], rules[1], (uint64_t)count * sizeof(uint32_t));
#endif
        tier1_select_test_expect(test, memcmp(states[0], states[1], (uint64_t)count * sizeof(uint32_t)) == 0, "the vector labeling is its scalar twin's on random columns");
        tier1_select_test_expect(test, memcmp(rules[0], rules[1], (uint64_t)count * sizeof(uint32_t)) == 0, "the vector rule choice is its scalar twin's on random columns");
        free(columns);
    }
}

// The IR's text fixtures (tests/ir and tests/ir/optimize), parsed, verified and checked by the twins.
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
                if (ir_parse_test_read(path, &source, &text) && ir_parse(text, &input, &scratch, &function).error == IR_PARSE_OK && ir_verify_scalar(&function, &scratch, 0).error == IR_OK)
                {
                    tier1_burs_check_function(&check, &function, &scratch);
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
    tier1_select_test_expect(test, files != 0 && check.mismatches == 0 && check.uncovered == 0, "the IR fixtures' classes, states and rules are the twins' alike, every covered class with a candidate");
    // As the selection's own summary: on standard error where standard output carries the manifest (--manifest).
    fprintf(test->manifest ? stderr : stdout, "tier1 bottom-up rewrite: %u states, %u rules, at most %u representers in a position, a pool of %u entries; the IR fixtures: %" PRIu64 " functions, %" PRIu64 " nodes, %" PRIu64 " rules' candidates, %" PRIu64 " munch's, %" PRIu64
           " mismatches, %" PRIu64 " uncovered\n",
           tier1_burs_state_count, tier1_burs_rule_count, tier1_burs_test_representers(), tier1_burs_pool_length, check.functions, check.nodes, check.matched, check.fallbacks, check.mismatches, check.uncovered);
    arena_release(&scratch);
    arena_release(&input);
    arena_release(&source);
}

// Addresses around a disp32's edges (tier1_burs_displacement): constant indices and offsets inside and past 32 bits, at
// every scale, among them pairs past it whose sum fits, which the vector classifier leaves to the scalar one; an index
// that isn't a constant and none, with offsets on both sides of the edge. The twins must agree, every address of a covered
// class must have its candidate, and both classes must occur.
static void tier1_select_test_burs_addresses(Tier1SelectTest* test)
{
    static const int64_t values[] = { 0, 1, -1, 7, INT32_MAX, INT32_MIN, (int64_t)INT32_MAX + 1, (int64_t)INT32_MIN - 1, INT64_C(1) << 28, -(INT64_C(1) << 28), INT64_C(1) << 40, -(INT64_C(1) << 40), INT64_MAX, INT64_MIN };
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
    valid = valid && ir_parse((String8){ .pointer = source, .length = length }, &input, &scratch, &function).error == IR_PARSE_OK && ir_verify_scalar(&function, &scratch, 0).error == IR_OK;
    tier1_select_test_expect(test, valid, "the addresses' fixture parses and verifies");
    Tier1BursCheck check = { 0 };
    uint32_t covered = 0;
    uint32_t past = 0;
    if (valid)
    {
        tier1_burs_check_function(&check, &function, &scratch);
        for (uint32_t node = 0; node < function.instruction_count; node += 1)
        {
            const bool address = function.op[node] == IR_ADDRESS;
            const bool fits = address && tier1_burs_displacement(&function, node);
            covered += fits;
            past += address && !fits;
        }
    }
    tier1_select_test_expect(test, check.functions == 1 && check.mismatches == 0 && check.uncovered == 0 && covered != 0 && past != 0,
                             "addresses around a disp32's edges: the twins' classes alike, a candidate for each covered one, both classes");
    fprintf(test->manifest ? stderr : stdout, "tier1 bottom-up rewrite addresses: %u covered, %u past a disp32\n", covered, past);
    arena_release(&scratch);
    arena_release(&input);
    arena_release(&text);
}

static void tier1_select_test_burs(Tier1SelectTest* test)
{
    tier1_select_test_burs_tables(test);
    tier1_select_test_burs_fixtures(test);
    tier1_select_test_burs_addresses(test);
}
