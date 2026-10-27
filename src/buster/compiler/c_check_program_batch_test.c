// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Entry-for-entry comparison with the original task loop, independently of the semantic scheduler. Every
// input column ends at a protected page, including all partial operator batches and the 64-node window edges.

#define BUSTER_CHECK_BATCH_TEST_NODES 321u
#define BUSTER_CHECK_BATCH_TEST_ENTRIES (BUSTER_CHECK_BATCH_TEST_NODES * 4)

static void check_test_program_batch(CheckTest* test)
{
    CheckTestTypes types = { 0 };
    bool ready = check_test_types_open(&types);
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint32_t sizes[] = {
        sizeof(CheckOp),
        sizeof(uint32_t),
        sizeof(uint32_t),
        sizeof(uint32_t),
        sizeof(CheckValue),
        sizeof(CheckNodeConversions),
    };
    uint8_t* mappings[BUSTER_ARRAY_LENGTH(sizes)] = { 0 };
    uint64_t lengths[BUSTER_ARRAY_LENGTH(sizes)] = { 0 };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(sizes); c += 1)
    {
        lengths[c] = ((uint64_t)sizes[c] * BUSTER_CHECK_BATCH_TEST_NODES + page - 1) / page * page;
        mappings[c] = mmap(0, lengths[c] + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = mappings[c] != MAP_FAILED && ready;
        if (mappings[c] != MAP_FAILED)
        {
            ready = mprotect(mappings[c] + lengths[c], page, PROT_NONE) == 0 && ready;
        }
    }
    uint8_t type_classes[256];
    uint8_t type_facts[256];
    uint32_t type_templates[256];
    ready = ready && types.table.types.count < BUSTER_ARRAY_LENGTH(type_classes);
    if (ready)
    {
        check_lower_classify_types_scalar(&types.table, types.table.types.count, type_classes, type_facts);
        for (uint32_t i = 0; i < types.table.types.count; i += 1)
        {
            type_templates[i] = type_classes[i] | (uint32_t)type_facts[i] << 8;
        }
    }
    check_test_expect(test, ready, "expression templates have guarded graph, value and conversion columns");
    uint32_t locals[2] = {
        BUSTER_CHECK_NONE,
        0,
    };
    SymbolId symbols[2] = {
        0,
        1,
    };
    uint8_t object_flags[1] = { 0 };
    const CheckOp binary[] = {
        CHECK_OP_ADD,
        CHECK_OP_SUBTRACT,
        CHECK_OP_MULTIPLY,
        CHECK_OP_DIVIDE,
        CHECK_OP_REMAINDER,
        CHECK_OP_SHIFT_LEFT,
        CHECK_OP_SHIFT_RIGHT,
        CHECK_OP_LESS,
        CHECK_OP_LESS_EQUAL,
        CHECK_OP_GREATER,
        CHECK_OP_GREATER_EQUAL,
        CHECK_OP_EQUAL,
        CHECK_OP_NOT_EQUAL,
        CHECK_OP_BIT_AND,
        CHECK_OP_BIT_OR,
        CHECK_OP_BIT_XOR,
    };
    for (uint32_t count = 1; ready && count <= BUSTER_CHECK_BATCH_TEST_NODES; count += 1)
    {
        CheckInput input = {
            .compound_objects = object_flags,
            .static_objects = object_flags,
        };
        CheckGraph* graph = &input.graph;
        graph->count = count;
        graph->ops = (CheckOp*)(mappings[0] + lengths[0] - (uint64_t)count * sizes[0]);
        graph->operand0 = (uint32_t*)(mappings[1] + lengths[1] - (uint64_t)count * sizes[1]);
        graph->operand1 = (uint32_t*)(mappings[2] + lengths[2] - (uint64_t)count * sizes[2]);
        graph->tokens = (uint32_t*)(mappings[3] + lengths[3] - (uint64_t)count * sizes[3]);
        CheckStream stream = {
            .values = (CheckValue*)(mappings[4] + lengths[4] - (uint64_t)count * sizes[4]),
            .conversions = (CheckNodeConversions*)(mappings[5] + lengths[5] - (uint64_t)count * sizes[5]),
            .count = count,
        };
        CheckProgram program = {
            .input = &input,
            .stream = &stream,
            .types = &types.table,
            .object_count = 2,
            .object_symbols = symbols,
        };
        // Plain postorder, promoted edges, folded subtrees (gaps), and repeated/reordered operands. Each mode
        // still compares the actual scalar task output; fallback must not hide a stricter eligibility rule.
        for (uint32_t mode = 0; mode < 5; mode += 1)
        {
            memset(stream.values, 0, (uint64_t)count * sizeof(CheckValue));
            memset(stream.conversions, 0, (uint64_t)count * sizeof(CheckNodeConversions));
            for (uint32_t n = 0; n < count; n += 1)
            {
                graph->tokens[n] = 1000 + n;
                graph->operand0[n] = BUSTER_CHECK_NONE;
                graph->operand1[n] = BUSTER_CHECK_NONE;
                graph->ops[n] = CHECK_OP_VALUE;
                stream.values[n].type = mode == 1 && n % 3 == 2 ? BUSTER_TYPE_CHAR : BUSTER_TYPE_INT;
                stream.values[n].category = CHECK_RVALUE;
                stream.values[n].constant_kind = CHECK_CONSTANT_INTEGER;
                stream.values[n].integer = UINT64_C(0x12345678ffffffd3) + n;
                if (n == 0)
                {
                    stream.values[n].category = CHECK_LVALUE;
                    stream.values[n].constant_kind = CHECK_CONSTANT_NONE;
                    stream.values[n].object = 1;
                }
                else if (n % 3 != 2)
                {
                    const bool unary = n % 3 == 1;
                    graph->ops[n] = unary ? (n % 2 != 0 ? CHECK_OP_NEGATIVE : CHECK_OP_BIT_NOT) : binary[(n / 3) % BUSTER_ARRAY_LENGTH(binary)];
                    graph->operand0[n] = n - (unary ? 1 : 2);
                    graph->operand1[n] = unary ? BUSTER_CHECK_NONE : n - 1;
                    stream.values[n].constant_kind = CHECK_CONSTANT_NONE;
                    stream.conversions[n].operands[0].type = BUSTER_TYPE_INT;
                    stream.conversions[n].operands[1].type = BUSTER_TYPE_INT;
                    if (mode == 2 && n % 9 == 0)
                    {
                        stream.values[n].constant_kind = CHECK_CONSTANT_INTEGER;
                    }
                    else if (mode == 3 && !unary)
                    {
                        graph->operand0[n] = n - 1;
                        graph->operand1[n] = n - 1;
                    }
                    else if (mode == 4 && !unary)
                    {
                        graph->operand0[n] = n - 1;
                        graph->operand1[n] = n - 2;
                    }
                }
            }
            uint8_t kinds[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint8_t classes[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint64_t values[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            CheckLowerTask tasks[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint32_t task_sources[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint32_t tokens[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            for (uint32_t limit = 0; limit < 3; limit += 1)
            {
                CheckProgramLowered status[3] = { 0 };
                CheckLowerWriter writers[3];
                for (uint32_t path = 0; path < 3; path += 1)
                {
                    writers[path] = (CheckLowerWriter){
                        .program = &program,
                        .output = status + path,
                        .tasks = tasks[path],
                        .kinds = kinds[path],
                        .classes = classes[path],
                        .values = values[path],
                        .tokens = tokens[path],
                        .task_sources = task_sources[path],
                        .local_map = locals,
                        .capacity = limit == 0 ? BUSTER_CHECK_BATCH_TEST_ENTRIES : limit == 1 ? count : 1,
                        .scalar = path == 0,
                        .type_classes = type_classes,
                        .type_facts = type_facts,
                        .type_templates = path == 2 ? NULL : type_templates,
                        .type_count = types.table.types.count,
                    };
                    check_lower_batch_reset(writers + path);
                    check_lower_expression(writers + path, count - 1, false, 0);
                    check_lower_drain(writers + path);
                }
                const bool same = writers[0].count == writers[1].count && status[0].status == status[1].status && status[0].token == status[1].token &&
                                  writers[0].token == writers[1].token && memcmp(kinds[0], kinds[1], writers[0].count) == 0 &&
                                  memcmp(classes[0], classes[1], writers[0].count) == 0 &&
                                  memcmp(values[0], values[1], (uint64_t)writers[0].count * sizeof(uint64_t)) == 0;
                char description[200];
                snprintf(description, sizeof(description), "expression templates: %u guarded nodes, mode %u, capacity %u; entries, status and diagnostic token agree", count,
                         mode, writers[0].capacity);
                check_test_expect(test, same, description);
                const bool fallback = writers[0].count == writers[2].count && status[0].status == status[2].status && status[0].token == status[2].token &&
                                      writers[0].token == writers[2].token && memcmp(kinds[0], kinds[2], writers[0].count) == 0 &&
                                      memcmp(classes[0], classes[2], writers[0].count) == 0 &&
                                      memcmp(values[0], values[2], (uint64_t)writers[0].count * sizeof(uint64_t)) == 0 && writers[2].classified_nodes == 0;
                check_test_expect(test, fallback, "without the optional template type table, vector lowering retains the exact task loop and failures");
                if (limit == 0)
                {
                    check_test_expect(test, writers[0].expression_nodes == writers[1].expression_nodes && writers[1].batch_nodes != 0,
                                      "expression coverage counts evaluated occurrences and the batch path decides ordinary nodes itself");
                    if (mode == 0 && count <= BUSTER_CHECK_LOWER_BATCH)
                    {
                        check_test_expect(test, writers[1].batch_nodes == writers[1].expression_nodes,
                                          "a contiguous ordinary subtree entirely takes templates, including every partial batch");
                    }
                }
            }
        }
    }
    // A speculative window's next node belongs to another lane's body. Its local_map index is deliberately
    // inaccessible: reading it would be both an out-of-bounds access here and a race in a concurrent lowering.
    if (ready)
    {
        CheckOp ops[2] = {
            CHECK_OP_VALUE,
            CHECK_OP_VALUE,
        };
        uint32_t operands[2] = {
            BUSTER_CHECK_NONE,
            BUSTER_CHECK_NONE,
        };
        uint32_t source_tokens[2] = {
            0,
            1,
        };
        uint32_t declarations[3] = {
            BUSTER_CHECK_NONE,
            BUSTER_CHECK_NONE,
            BUSTER_CHECK_NONE,
        };
        uint32_t bodies[2] = {
            10,
            20,
        };
        uint8_t flags[2] = { 0 };
        uint8_t static_flags[2] = { 0 };
        SymbolId object_symbols[3] = { 0 };
        CheckValue source_values[2] = {
            {
                .type = BUSTER_TYPE_INT,
                .category = CHECK_LVALUE,
                .object = 1,
            },
            {
                .type = BUSTER_TYPE_INT,
                .category = CHECK_LVALUE,
                .object = 2,
            },
        };
        CheckNodeConversions conversions[2] = { 0 };
        CheckInput input = {
            .graph = {
                .ops = ops,
                .tokens = source_tokens,
                .operand0 = operands,
                .operand1 = operands,
                .count = 2,
            },
            .token_count = 2,
            .compound_objects = flags,
            .static_objects = static_flags,
            .declaration_bodies = bodies,
        };
        CheckStream stream = {
            .values = source_values,
            .conversions = conversions,
            .count = 2,
        };
        CheckProgram program = {
            .input = &input,
            .stream = &stream,
            .types = &types.table,
            .object_count = 3,
            .object_symbols = object_symbols,
            .object_declarations = declarations,
        };
        CheckProgramBody body = { .opening = 10 };
        CheckProgramLowered status = { 0 };
        uint32_t* local_map = (uint32_t*)(mappings[1] + lengths[1] - 2 * sizeof(uint32_t));
        local_map[0] = BUSTER_CHECK_NONE;
        local_map[1] = 0;
        uint8_t kinds[2];
        uint8_t classes[2];
        uint64_t values[2];
        CheckLowerWriter writer = {
            .program = &program,
            .output = &status,
            .body = &body,
            .local_map = local_map,
            .kinds = kinds,
            .classes = classes,
            .values = values,
            .capacity = 2,
            .type_classes = type_classes,
            .type_facts = type_facts,
            .type_templates = type_templates,
            .type_count = types.table.types.count,
        };
        check_lower_batch_reset(&writer);
        const bool lowered = check_lower_batch_expression(&writer, 0);
        check_test_expect(test, lowered && writer.count == 1 && kinds[0] == BUSTER_TYPED_LOCAL && classes[0] == BUSTER_TYPED_CLASS_I32 && values[0] == 0,
                          "a window across bodies never reads another lane's mutable local map");
        flags[0] = 1;
        source_values[0].category = CHECK_RVALUE;
        source_values[0].constant_kind = CHECK_CONSTANT_INTEGER;
        source_values[0].integer = 7;
        writer.count = 0;
        check_lower_batch_reset(&writer);
        check_test_expect(test, !check_lower_batch_expression(&writer, 0) && writer.count == 0,
                          "an automatic compound literal always defers its initializer, before testing constant proofs");
    }
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(sizes); c += 1)
    {
        if (mappings[c] != NULL && mappings[c] != MAP_FAILED)
        {
            munmap(mappings[c], lengths[c] + page);
        }
    }
    check_test_types_close(&types);
}
