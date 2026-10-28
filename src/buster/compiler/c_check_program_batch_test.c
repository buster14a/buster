// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Entry-for-entry comparison with the original task loop, independently of the semantic scheduler. Every
// input column ends at a protected page, including all partial operator batches and the 16-node window edges.

#define BUSTER_CHECK_BATCH_TEST_NODES 321u
#define BUSTER_CHECK_BATCH_TEST_ENTRIES (BUSTER_CHECK_BATCH_TEST_NODES * 4)

static void check_test_program_task_stack(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* memory = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0;
    CheckOp leaf_op = CHECK_OP_VALUE;
    uint32_t leaf_token = 1234;
    uint32_t no_operand = BUSTER_CHECK_NONE;
    CheckValue leaf = { .type = BUSTER_TYPE_INT, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 7 };
    CheckNodeConversions conversion = { 0 };
    CheckInput input = { .graph = { .ops = &leaf_op, .tokens = &leaf_token, .operand0 = &no_operand, .operand1 = &no_operand, .count = 1 } };
    CheckStream stream = { .values = &leaf, .conversions = &conversion, .count = 1 };
    CheckProgram program = { .input = &input, .stream = &stream };
    uint8_t type_classes[256] = { 0 };
    uint8_t type_facts[256] = { 0 };
    uint32_t type_templates[256] = { 0 };
    type_classes[type_index(BUSTER_TYPE_INT)] = BUSTER_TYPED_CLASS_I32;
    type_templates[type_index(BUSTER_TYPE_INT)] = BUSTER_TYPED_CLASS_I32;
    check_test_expect(test, ready, "pending lowering tasks have a protected stack tail");
    for (uint32_t count = 1; ready && count <= 17; count += 1)
    {
        CheckLowerTaskPacked* tasks = (CheckLowerTaskPacked*)(memory + page - count * sizeof(CheckLowerTaskPacked));
        for (uint32_t mode = 0; mode < 7; mode += 1)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                const bool label = mode == 1 && i == count / 2;
                const bool expression = mode >= 3 && i % 3 == 0;
                tasks[count - 1 - i] = check_lower_task_pack((CheckLowerTask){
                    .kind = expression ? CHECK_LOWER_EXPRESSION : label ? CHECK_LOWER_LABEL : CHECK_LOWER_INSTRUCTION,
                    .node = expression || label ? 0 : i % 2 == 0 ? BUSTER_TYPED_CONST : BUSTER_TYPED_NEG,
                    .type = expression || label ? 0 : (mode == 2 || mode == 4) && i == count / 2 ? UINT8_MAX : i % BUSTER_TYPED_CLASSES,
                    .value = expression || label ? 0 : UINT64_C(0x1234567800000000) + i,
                    .discard = expression && mode == 5 && i == count / 2,
                    .address = expression && mode == 6 && i == count / 2,
                });
            }
            for (uint32_t capacity = 0; capacity <= count + 1; capacity += 1)
            {
                uint8_t kinds[2][32];
                uint8_t classes[2][32];
                uint64_t values[2][32];
                uint32_t labels[2][1] = { { BUSTER_CHECK_NONE }, { BUSTER_CHECK_NONE } };
                memset(kinds, 0xa5, sizeof(kinds));
                memset(classes, 0xa5, sizeof(classes));
                memset(values, 0xa5, sizeof(values));
                CheckProgramLowered status[2] = { 0 };
                CheckLowerWriter writers[2];
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    writers[path] = (CheckLowerWriter){
                        .output = status + path,
                        .program = &program,
                        .tasks = tasks,
                        .depth = count,
                        .capacity = capacity,
                        .kinds = kinds[path],
                        .classes = classes[path],
                        .values = values[path],
                        .labels = labels[path],
                        .label_count = 1,
                        .token = 123,
                        .scalar = path == 0,
                        .type_classes = type_classes,
                        .type_facts = type_facts,
                        .type_templates = type_templates,
                        .type_count = BUSTER_ARRAY_LENGTH(type_classes),
                    };
                    check_lower_drain(writers + path);
                }
                check_test_expect(test, status[0].status == status[1].status && status[0].token == status[1].token &&
                                  writers[0].count == writers[1].count && writers[0].depth == writers[1].depth && labels[0][0] == labels[1][0] &&
                                  writers[0].token == writers[1].token && writers[0].expression_nodes == writers[1].expression_nodes &&
                                  memcmp(kinds[0], kinds[1], sizeof(kinds[0])) == 0 && memcmp(classes[0], classes[1], sizeof(classes[0])) == 0 &&
                                  memcmp(values[0], values[1], sizeof(values[0])) == 0,
                                  "guarded instruction batches preserve columns, untouched tails, labels and the first capacity/class failure");
                if (mode == 0 && capacity == count)
                {
                    bool expected = status[0].status == CHECK_PROGRAM_OK && writers[0].count == count;
                    for (uint32_t i = 0; expected && i < count; i += 1)
                    {
                        expected = kinds[0][i] == (i % 2 == 0 ? BUSTER_TYPED_CONST : BUSTER_TYPED_NEG) &&
                                   classes[0][i] == i % BUSTER_TYPED_CLASSES && values[0][i] == UINT64_C(0x1234567800000000) + i;
                    }
                    check_test_expect(test, expected, "hot task-field decoding also agrees with independent full-width entry literals");
                }
            }
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, 2 * page);
    }
}

static void check_test_program_batch(CheckTest* test)
{
    check_test_program_task_stack(test);
    for (uint32_t kind = CHECK_LOWER_EXPRESSION; kind <= CHECK_LOWER_COMPLEX_TRUTH; kind += 1)
    {
        for (uint32_t flags = 0; flags < 16; flags += 1)
        {
            const bool full = kind == CHECK_LOWER_INSTRUCTION || kind == CHECK_LOWER_ADDRESS || kind == CHECK_LOWER_COPY || kind == CHECK_LOWER_COMPLEX;
            const CheckLowerTask original = {
                .kind = (CheckLowerTaskKind)kind,
                .node = UINT32_MAX - kind,
                .type = kind == CHECK_LOWER_INSTRUCTION ? UINT8_MAX : full ? 0 : UINT32_MAX - flags,
                .value = full ? UINT64_MAX - flags : UINT32_MAX - kind,
                .address = (flags & 1) != 0,
                .initialized = (flags & 2) != 0,
                .discard = (flags & 4) != 0,
                .volatile_copy = (flags & 8) != 0,
            };
            const CheckLowerTask decoded = check_lower_task_unpack(check_lower_task_pack(original));
            check_test_expect(test, decoded.kind == original.kind && decoded.node == original.node && decoded.type == original.type &&
                              decoded.value == original.value && decoded.address == original.address && decoded.initialized == original.initialized &&
                              decoded.discard == original.discard && decoded.volatile_copy == original.volatile_copy,
                              "two-word pending tasks retain full node/type identities, payload bits and independent flags");
        }
    }
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
        const CheckLowerWriter columns = {
            .type_classes = type_classes,
            .type_facts = type_facts,
            .type_count = types.table.types.count,
        };
        CheckLowerWriter packed = columns;
        packed.type_templates = type_templates;
        bool same = true;
        for (uint32_t a = 0; a <= types.table.types.count; a += 1)
        {
            for (uint32_t b = 0; b <= types.table.types.count; b += 1)
            {
                for (uint32_t qualifiers = 0; qualifiers < 8; qualifiers += 1)
                {
                    const TypeId from = a << BUSTER_TYPE_INDEX_SHIFT | qualifiers;
                    const TypeId to = b << BUSTER_TYPE_INDEX_SHIFT | (7 - qualifiers);
                    same = same && check_lower_conversion_empty(&columns, from, to) == check_lower_conversion_empty(&packed, from, to);
                }
            }
        }
        check_test_expect(test, same, "packed conversion lookup matches separate class/fact columns for every type pair, qualifier and out-of-range row");
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
        for (uint32_t mode = 0; mode < 10; mode += 1)
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
                if (mode >= 7 && n != 0)
                {
                    const TypeId numeric[] = { BUSTER_TYPE_INT, BUSTER_TYPE_UCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_FLOAT };
                    graph->ops[n] = n % 2 != 0 ? CHECK_OP_CAST : CHECK_OP_CONVERT;
                    graph->operand0[n] = mode == 9 && n > 1 && n % 3 == 0 ? n - 2 : n - 1;
                    graph->operand1[n] = BUSTER_CHECK_NONE;
                    stream.values[n].constant_kind = CHECK_CONSTANT_NONE;
                    stream.values[n].type = mode == 8 && n % 4 == 1 ? BUSTER_TYPE_BOOL : numeric[n % BUSTER_ARRAY_LENGTH(numeric)];
                }
            }
            uint8_t kinds[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint8_t classes[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint64_t values[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            CheckLowerTaskPacked tasks[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint32_t task_sources[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint32_t tokens[3][BUSTER_CHECK_BATCH_TEST_ENTRIES];
            uint8_t indirect[3][1] = { { 0 } };
            uint8_t local_flags[3][1] = { { 0 } };
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
                        .indirect = indirect[path],
                        .local_flags = local_flags[path],
                        .capacity = limit == 0 ? BUSTER_CHECK_BATCH_TEST_ENTRIES : limit == 1 ? count : 1,
                        .scalar = path == 0,
                        .type_classes = type_classes,
                        .type_facts = type_facts,
                        .type_templates = path == 2 ? NULL : type_templates,
                        .type_count = types.table.types.count,
                    };
                    indirect[path][0] = mode == 6 ? BUSTER_CHECK_LOWER_VLA_ALLOCATED : BUSTER_CHECK_LOWER_VLA_NONE;
                    local_flags[path][0] = 0;
                    if (path == 1)
                    {
                        const uint32_t first = count > BUSTER_CHECK_LOWER_BATCH ? count - BUSTER_CHECK_LOWER_BATCH : 0;
                        uint32_t classified[BUSTER_CHECK_LOWER_BATCH];
                        _mm512_storeu_si512(classified, check_lower_batch_classify(writers + path, first, count - first));
                        bool same_metadata = true;
                        for (uint32_t i = 0; i < count - first; i += 1)
                        {
                            const uint32_t index = type_index(stream.values[first + i].type);
                            const uint32_t expected = index < types.table.types.count ? type_templates[index] : UINT8_MAX;
                            same_metadata = same_metadata && (classified[i] & UINT16_MAX) == expected;
                        }
                        check_test_expect(test, same_metadata, "every guarded classification lane agrees with the scalar type facts");
                    }
                    check_lower_expression(writers + path, count - 1, mode == 5 || mode == 6, 0);
                    check_lower_drain(writers + path);
                }
                const bool same = writers[0].count == writers[1].count && status[0].status == status[1].status && status[0].token == status[1].token &&
                                  writers[0].token == writers[1].token && memcmp(kinds[0], kinds[1], writers[0].count) == 0 &&
                                  memcmp(classes[0], classes[1], writers[0].count) == 0 &&
                                  memcmp(values[0], values[1], (uint64_t)writers[0].count * sizeof(uint64_t)) == 0 && local_flags[0][0] == local_flags[1][0];
                char description[200];
                snprintf(description, sizeof(description), "expression templates: %u guarded nodes, mode %u, capacity %u; entries, status and diagnostic token agree", count,
                         mode, writers[0].capacity);
                check_test_expect(test, same, description);
                const bool fallback = writers[0].count == writers[2].count && status[0].status == status[2].status && status[0].token == status[2].token &&
                                      writers[0].token == writers[2].token && memcmp(kinds[0], kinds[2], writers[0].count) == 0 &&
                                      memcmp(classes[0], classes[2], writers[0].count) == 0 &&
                                      memcmp(values[0], values[2], (uint64_t)writers[0].count * sizeof(uint64_t)) == 0 && local_flags[0][0] == local_flags[2][0] && writers[2].classified_nodes == 0;
                check_test_expect(test, fallback, "without the optional template type table, vector lowering retains the exact entries and failures");
                if (limit == 0)
                {
                    check_test_expect(test, writers[0].expression_nodes == writers[1].expression_nodes && writers[1].batch_nodes <= writers[1].expression_nodes,
                                      "expression coverage counts evaluated occurrences and the batch path decides ordinary nodes itself");
                    if ((mode == 0 || mode == 1) && graph->ops[count - 1] != CHECK_OP_VALUE)
                    {
                        check_test_expect(test, writers[1].batch_nodes != 0, "an ordinary operation takes its vector-classified native template");
                    }
                    if (mode == 0 && count == 2)
                    {
                        check_test_expect(test, writers[1].batch_nodes == 2, "a unary leaf pair shares a bulk output template");
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
        (void)check_lower_batch_classify(&writer, 0, 2);
        const bool lowered = check_lower_value(&writer, 0, false, false);
        check_test_expect(test, lowered && writer.count == 1 && kinds[0] == BUSTER_TYPED_LOCAL && classes[0] == BUSTER_TYPED_CLASS_I32 && values[0] == 0,
                          "a window across bodies never reads another lane's mutable local map");
        flags[0] = 1;
        source_values[0].category = CHECK_RVALUE;
        source_values[0].constant_kind = CHECK_CONSTANT_INTEGER;
        source_values[0].integer = 7;
        writer.count = 0;
        check_test_expect(test, !check_lower_value(&writer, 0, false, false) && writer.count == 0,
                          "an automatic compound literal always defers its initializer, before testing constant proofs");
        check_test_expect(test, !check_lower_value(&writer, 0, true, false) && writer.count == 0,
                          "an automatic compound literal also initializes before an addressed leaf");
        check_test_expect(test, check_lower_value(&writer, 0, false, true) && writer.count == 1 && kinds[0] == BUSTER_TYPED_CONST && values[0] == 7,
                          "an initialized compound expression can then take its constant leaf template");
        ops[0] = CHECK_OP_CAST;
        writer.count = 0;
        check_test_expect(test, check_lower_value(&writer, 0, false, false) && writer.count == 1 && values[0] == 7,
                          "a proven cast is a leaf without inspecting its skipped operand or another body's map");
        const CheckOp ordered[] = { CHECK_OP_STATEMENT_VALUE, CHECK_OP_INITIALIZE, CHECK_OP_BUILTIN_QUERY };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ordered); i += 1)
        {
            ops[0] = ordered[i];
            check_test_expect(test, !check_lower_value(&writer, 0, false, false) && writer.count == 1,
                              "a constant annotation never bypasses a region, initializer or query's evaluation rule");
        }
        ops[0] = CHECK_OP_CAST;
        source_values[0].flags = BUSTER_CHECK_SIDE_EFFECT;
        check_test_expect(test, !check_lower_value(&writer, 0, false, false), "a cast with side effects retains the scalar evaluation rule");
        source_values[0].flags = BUSTER_CHECK_FP_ENVIRONMENT;
        check_test_expect(test, !check_lower_value(&writer, 0, false, false), "a cast with floating environment effects retains runtime evaluation");
        source_values[0].flags = 0;
        source_values[0].fault = CHECK_FAULT_SIGNED_OVERFLOW;
        check_test_expect(test, !check_lower_value(&writer, 0, false, false), "a faulted constant retains the original lowering rule");
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
