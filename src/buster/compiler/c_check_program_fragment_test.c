// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Closed-fragment regression tests (#868). Compare the real scalar drain, normal dispatch and direct candidate;
// every graph and output column ends at a protected page. Rejected speculation must leave output untouched.

#define BUSTER_CHECK_FRAGMENT_TEST_NODES 17u
#define BUSTER_CHECK_FRAGMENT_TEST_ENTRIES 96u

static void check_test_program_fragments(CheckTest* test, TypeTable* types, uint8_t* type_classes, uint8_t* type_facts, uint32_t* type_templates)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint32_t sizes[] = {
        sizeof(CheckOp),
        sizeof(uint32_t),
        sizeof(uint32_t),
        sizeof(uint32_t),
        sizeof(CheckValue),
        sizeof(CheckNodeConversions),
    };
    uint8_t* memory[15] = { 0 };
    uint64_t lengths[15] = { 0 };
    bool ready = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(memory); i += 1)
    {
        const uint64_t bytes = i < 6 ? (uint64_t)sizes[i] * BUSTER_CHECK_FRAGMENT_TEST_NODES :
                                      (uint64_t)BUSTER_CHECK_FRAGMENT_TEST_ENTRIES * ((i - 6) % 3 == 2 ? sizeof(uint64_t) : 1);
        lengths[i] = (bytes + page - 1) / page * page;
        memory[i] = mmap(0, lengths[i] + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory[i] == MAP_FAILED)
        {
            ready = false;
        }
        else
        {
            ready = mprotect(memory[i] + lengths[i], page, PROT_NONE) == 0 && ready;
        }
    }
    check_test_expect(test, ready, "nested fragments have protected graph, conversion, payload and output tails");
    uint32_t locals[2] = {
        BUSTER_CHECK_NONE,
        0,
    };
    SymbolId symbols[2] = {
        0,
        1,
    };
    uint8_t object_flags[1] = { 0 };
    const TypeId integers[] = {
        BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_ULONG,
    };
    for (uint32_t count = 3; ready && count <= BUSTER_CHECK_FRAGMENT_TEST_NODES; count += 1)
    {
        CheckInput input = {
            .compound_objects = object_flags,
            .static_objects = object_flags,
        };
        CheckGraph* graph = &input.graph;
        graph->count = count;
        graph->ops = (CheckOp*)(memory[0] + lengths[0] - (uint64_t)count * sizes[0]);
        graph->operand0 = (uint32_t*)(memory[1] + lengths[1] - (uint64_t)count * sizes[1]);
        graph->operand1 = (uint32_t*)(memory[2] + lengths[2] - (uint64_t)count * sizes[2]);
        graph->tokens = (uint32_t*)(memory[3] + lengths[3] - (uint64_t)count * sizes[3]);
        CheckStream stream = {
            .values = (CheckValue*)(memory[4] + lengths[4] - (uint64_t)count * sizes[4]),
            .conversions = (CheckNodeConversions*)(memory[5] + lengths[5] - (uint64_t)count * sizes[5]),
            .count = count,
        };
        CheckProgram program = {
            .input = &input,
            .stream = &stream,
            .types = types,
            .object_count = 2,
            .object_symbols = symbols,
        };
        for (uint32_t shape = 0; shape < 5; shape += 1)
        {
            const TypeId integer = integers[(count + shape) % BUSTER_ARRAY_LENGTH(integers)];
            const uint8_t integer_class = type_classes[type_index(integer)];
            const bool selected = shape < 2 || (shape < 4 && count % 2 != 0) || (shape == 4 && count == 7);
            for (uint32_t mode = 0; selected && mode < (shape == 4 ? 7u : 1u); mode += 1)
            {
                memset(stream.values, 0, (uint64_t)count * sizeof(CheckValue));
                memset(stream.conversions, 0, (uint64_t)count * sizeof(CheckNodeConversions));
                uint32_t entries = 0;
                for (uint32_t n = 0; n < count; n += 1)
                {
                    const uint32_t leaf_count = (count + 1) / 2;
                    const bool leaf = shape < 2 ? n == 0 : shape == 2 ? n == 0 || n % 2 != 0 :
                                      shape == 3 ? n < leaf_count : n == 0 || n == 1 || n == 3 || n == 4;
                    graph->tokens[n] = 2000 + n;
                    graph->ops[n] = leaf ? CHECK_OP_VALUE : shape == 1 && n + 1 != count && n % 3 != 0 ? CHECK_OP_CAST :
                                    shape < 2 ? CHECK_OP_NEGATIVE : n + 1 == count ? CHECK_OP_BIT_XOR : CHECK_OP_SUBTRACT;
                    graph->operand0[n] = leaf ? BUSTER_CHECK_NONE : shape < 2 ? n - 1 : shape == 3 ? count - 1 - n : shape == 4 && n == 6 ? 2 : n - 2;
                    graph->operand1[n] = leaf || shape < 2 ? BUSTER_CHECK_NONE : n - 1;
                    stream.values[n].type = integer;
                    stream.values[n].category = n == 0 ? CHECK_LVALUE : CHECK_RVALUE;
                    stream.values[n].constant_kind = leaf && n != 0 ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE;
                    stream.values[n].integer = 17 + n;
                    stream.values[n].object = n == 0 ? 1 : 0;
                    stream.conversions[n].operands[0].type = integer;
                    stream.conversions[n].operands[1].type = integer;
                    entries += graph->ops[n] != CHECK_OP_CAST;
                }
                if (mode == 1)
                {
                    stream.values[4].type = BUSTER_TYPE_CHAR;
                }
                else if (mode == 2)
                {
                    graph->operand1[count - 1] = graph->operand0[count - 1];
                }
                else if (mode == 3)
                {
                    graph->operand0[count - 1] = 5;
                    graph->operand1[count - 1] = 2;
                }
                else if (mode == 4)
                {
                    stream.values[count - 1].flags = BUSTER_CHECK_SIDE_EFFECT;
                }
                else if (mode == 5)
                {
                    stream.values[5].constant_kind = CHECK_CONSTANT_INTEGER;
                    stream.values[5].integer = 42;
                }
                else if (mode == 6)
                {
                    graph->ops[5] = CHECK_OP_SHIFT_LEFT;
                }
                for (uint32_t limit = 0; limit < 6; limit += 1)
                {
                    const uint32_t capacity = limit == 1 ? count : limit == 2 ? 2 : limit == 3 ? 3 * count - 1 : limit == 4 ? 3 * count : BUSTER_CHECK_FRAGMENT_TEST_ENTRIES;
                    const uint32_t initial = limit == 5 ? capacity - entries : 0;
                    CheckProgramLowered status[3] = { 0 };
                    CheckLowerWriter writers[3];
                    CheckLowerTaskPacked tasks[3][BUSTER_CHECK_FRAGMENT_TEST_ENTRIES];
                    uint8_t indirect[3][1] = { { 0 } };
                    uint8_t local_flags[3][1] = { { 0 } };
                    for (uint32_t path = 0; path < 3; path += 1)
                    {
                        uint8_t* kinds = memory[6 + path * 3] + lengths[6 + path * 3] - BUSTER_CHECK_FRAGMENT_TEST_ENTRIES;
                        uint8_t* classes = memory[7 + path * 3] + lengths[7 + path * 3] - BUSTER_CHECK_FRAGMENT_TEST_ENTRIES;
                        uint64_t* values = (uint64_t*)(memory[8 + path * 3] + lengths[8 + path * 3] - BUSTER_CHECK_FRAGMENT_TEST_ENTRIES * sizeof(uint64_t));
                        memset(kinds, 0xa5, BUSTER_CHECK_FRAGMENT_TEST_ENTRIES);
                        memset(classes, 0xa5, BUSTER_CHECK_FRAGMENT_TEST_ENTRIES);
                        memset(values, 0xa5, BUSTER_CHECK_FRAGMENT_TEST_ENTRIES * sizeof(uint64_t));
                        writers[path] = (CheckLowerWriter){
                            .program = &program,
                            .output = status + path,
                            .tasks = tasks[path],
                            .kinds = kinds,
                            .classes = classes,
                            .values = values,
                            .local_map = locals,
                            .indirect = indirect[path],
                            .local_flags = local_flags[path],
                            .capacity = capacity,
                            .count = initial,
                            .token = 123,
                            .scalar = path == 0,
                            .type_classes = type_classes,
                            .type_facts = type_facts,
                            .type_templates = type_templates,
                            .type_count = types->types.count,
                            .class_limit = BUSTER_TYPED_CLASSES,
                        };
                        bool accepted = false;
#if BUSTER_HOST_VECTOR
                        if (path == 2)
                        {
                            accepted = check_lower_fragment(writers + path, count - 1);
                            const bool expected = mode == 0 && count <= BUSTER_CHECK_LOWER_FRAGMENT_NODES && capacity >= 3 * count;
                            check_test_expect(test, accepted == expected, "a closed integer fragment is accepted exactly within the node and capacity bounds");
                            if (!accepted)
                            {
                                bool untouched = writers[path].count == initial && writers[path].token == 123 && writers[path].depth == 0 &&
                                                 writers[path].expression_nodes == 0 && writers[path].batch_nodes == 0 && status[path].status == CHECK_PROGRAM_OK;
                                for (uint32_t i = 0; i < BUSTER_CHECK_FRAGMENT_TEST_ENTRIES; i += 1)
                                {
                                    untouched = untouched && kinds[i] == 0xa5 && classes[i] == 0xa5 && values[i] == UINT64_C(0xa5a5a5a5a5a5a5a5);
                                }
                                check_test_expect(test, untouched, "a rejected fragment does not publish entries, tasks, tokens or evaluated-node counts");
                            }
                            else
                            {
                                bool literal = writers[path].count == initial + entries && writers[path].expression_nodes == count && writers[path].batch_nodes == count;
                                uint32_t entry = initial;
                                uint32_t last_leaf = 0;
                                for (uint32_t n = 0; n < count; n += 1)
                                {
                                    const CheckOp op = graph->ops[n];
                                    const bool leaf = op == CHECK_OP_VALUE;
                                    if (leaf || op != CHECK_OP_CAST)
                                    {
                                        const uint8_t kind = leaf ? n == 0 ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_CONST :
                                                             op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : op == CHECK_OP_BIT_XOR ? BUSTER_TYPED_XOR : BUSTER_TYPED_SUB;
                                        literal = literal && kinds[entry] == kind && classes[entry] == integer_class && values[entry] == (leaf && n != 0 ? 17u + n : 0);
                                        entry += 1;
                                    }
                                    last_leaf = leaf ? n : last_leaf;
                                }
                                check_test_expect(test, literal && writers[path].token == graph->tokens[last_leaf], "nested entries, compacted wrapper holes and final tokens also agree with independent literals");
                            }
                        }
#endif
                        if (!accepted)
                        {
                            check_lower_expression(writers + path, count - 1, false, 0);
                        }
                        check_lower_drain(writers + path);
                    }
                    for (uint32_t path = 1; path < 3; path += 1)
                    {
                        const bool same = writers[0].count == writers[path].count && writers[0].depth == writers[path].depth &&
                                          writers[0].token == writers[path].token && status[0].status == status[path].status && status[0].token == status[path].token &&
                                          writers[0].expression_nodes == writers[path].expression_nodes && local_flags[0][0] == local_flags[path][0] &&
                                          memcmp(writers[0].kinds, writers[path].kinds, BUSTER_CHECK_FRAGMENT_TEST_ENTRIES) == 0 &&
                                          memcmp(writers[0].classes, writers[path].classes, BUSTER_CHECK_FRAGMENT_TEST_ENTRIES) == 0 &&
                                          memcmp(writers[0].values, writers[path].values, BUSTER_CHECK_FRAGMENT_TEST_ENTRIES * sizeof(uint64_t)) == 0;
                        char description[160];
                        snprintf(description, sizeof(description), "nested fragments: %u nodes, shape %u, mode %u, limit %u, path %u matches the scalar drain", count, shape, mode, limit, path);
                        check_test_expect(test, same, description);
                    }
                    if (BUSTER_HOST_VECTOR && mode == 0 && limit == 0 && count <= 16)
                    {
                        check_test_expect(test, writers[1].batch_nodes == count, "normal dispatch batches the entire nested root, not only its initial leaves");
                    }
                }
            }
        }
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(memory); i += 1)
    {
        if (memory[i] != NULL && memory[i] != MAP_FAILED)
        {
            munmap(memory[i], lengths[i] + page);
        }
    }
}
