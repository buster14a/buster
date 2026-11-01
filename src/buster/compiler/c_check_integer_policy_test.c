// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Integration checks for the separately oracle-qualified GNU integer folding extension.
#ifndef BUSTER_C_CHECK_INTEGER_POLICY_TEST_INCLUDED
#define BUSTER_C_CHECK_INTEGER_POLICY_TEST_INCLUDED

static void check_test_integer_policy_actions(CheckTest* test, TypeTable* table)
{
    const CheckOp operations[] = { CHECK_OP_NEGATIVE, CHECK_OP_ADD, CHECK_OP_SUBTRACT, CHECK_OP_MULTIPLY,
                                   CHECK_OP_DIVIDE, CHECK_OP_REMAINDER, CHECK_OP_SHIFT_LEFT, CHECK_OP_SHIFT_RIGHT,
                                   CHECK_OP_SHIFT_LEFT, CHECK_OP_SHIFT_LEFT };
    const uint32_t left[] = { 0x80000000, 0x7fffffff, 0x80000000, 50000, 0x80000000, 0x80000000, 1, 8, 0xffffffff, 3 };
    const uint32_t right[] = { 0, 1, 1, 50000, 0xffffffff, 0xffffffff, 32, 0xffffffff, 1, 31 };
    const uint32_t expected[] = { 0x80000000, 0x80000000, 0x7fffffff, 2500000000u, 0x80000000, 0, 0x80000000, 16, 0xfffffffe, 0x80000000 };
    const uint32_t warnings[] = { CHECK_WARNING_INTEGER_OVERFLOW, CHECK_WARNING_INTEGER_OVERFLOW,
                                 CHECK_WARNING_INTEGER_OVERFLOW, CHECK_WARNING_INTEGER_OVERFLOW, 0, 0,
                                 CHECK_WARNING_SHIFT_COUNT_OVERFLOW, CHECK_WARNING_SHIFT_COUNT_NEGATIVE,
                                 CHECK_WARNING_SHIFT_NEGATIVE_VALUE, CHECK_WARNING_SHIFT_OVERFLOW };
    CheckValue wrapped = { 0 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operations); i += 1)
    {
        CheckAction action = { .op = operations[i], .operands = { check_test_integer(BUSTER_TYPE_INT, left[i]),
                                                                 check_test_integer(BUSTER_TYPE_INT, right[i]) }, .node = 201 + i };
        const CheckActionResult strict = check_test_action(test, table, action);
        check_test_expect(test, strict.diagnostic == CHECK_DIAG_NONE && strict.value.constant_kind == CHECK_CONSTANT_NONE &&
                               strict.value.fault == (i < 6 ? CHECK_FAULT_SIGNED_OVERFLOW : i < 8 ? CHECK_FAULT_SHIFT_COUNT : CHECK_FAULT_SHIFT_VALUE) &&
                               strict.value.fault_node == action.node && strict.warnings == 0,
                          "strict action and vector lanes preserve the original overflow or shift fault");
        action.extensions = BUSTER_CHECK_EXTENSION_GNU_CONSTANT;
        const CheckActionResult gnu = check_test_action(test, table, action);
        check_test_expect(test, gnu.diagnostic == CHECK_DIAG_NONE && gnu.value.constant_kind == CHECK_CONSTANT_INTEGER &&
                               gnu.value.integer == expected[i] && gnu.value.fault == CHECK_FAULT_NONE &&
                               (gnu.value.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_GNU_ICE_FORM)) ==
                                   (BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_GNU_ICE_FORM) &&
                               gnu.warnings == warnings[i] && ((gnu.value.flags & BUSTER_CHECK_GNU_DIVISION) != 0) == (i == 4 || i == 5) &&
                               check_action_integer_constant(&gnu.value, action.extensions) && !check_action_integer_constant(&gnu.value, 0),
                          "explicit GNU policy publishes oracle-qualified bits with exact operation warnings and lexical ICE provenance");
        wrapped = i == 1 ? gnu.value : wrapped;
    }
    const CheckOp ancestors[] = { CHECK_OP_POSITIVE, CHECK_OP_LESS, CHECK_OP_LOGICAL_AND, CHECK_OP_CAST, CHECK_OP_CONDITIONAL,
                                  CHECK_OP_COMMA, CHECK_OP_STATEMENT_VALUE };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ancestors); i += 1)
    {
        const CheckAction action = { .op = ancestors[i], .operands = { wrapped, check_test_integer(BUSTER_TYPE_INT, 1), wrapped },
                                    .type = BUSTER_TYPE_LONG, .extensions = BUSTER_CHECK_EXTENSION_GNU_CONSTANT };
        const CheckActionResult result = check_test_action(test, table, action);
        check_test_expect(test, result.diagnostic == CHECK_DIAG_NONE && result.warnings == 0 &&
                               (result.value.flags & BUSTER_CHECK_GNU_CONSTANT) != 0 && (result.value.flags & BUSTER_CHECK_ICE) == 0 &&
                               ((result.value.flags & BUSTER_CHECK_GNU_ICE_FORM) != 0) == (i < 5),
                          "GNU witness survives ancestors without repeated warnings while comma and regions clear lexical ICE form");
    }
    CheckAction divide = { .op = CHECK_OP_DIVIDE, .operands = { wrapped, check_test_integer(BUSTER_TYPE_INT, 0) },
                           .extensions = BUSTER_CHECK_EXTENSION_GNU_CONSTANT, .node = 217 };
    const CheckActionResult failed = check_test_action(test, table, divide);
    check_test_expect(test, failed.value.fault == CHECK_FAULT_DIVIDE_BY_ZERO && failed.value.fault_node == divide.node &&
                           failed.value.constant_kind == CHECK_CONSTANT_NONE && failed.warnings == 0 &&
                           !check_action_integer_constant(&failed.value, divide.extensions),
                      "GNU overflow policy does not suppress fatal division faults or fabricate constants");
    divide.op = CHECK_OP_ADD;
    divide.operands[0] = check_test_integer(BUSTER_TYPE_INT, INT32_MAX);
    divide.operands[0].flags |= BUSTER_CHECK_SIDE_EFFECT;
    divide.operands[1] = check_test_integer(BUSTER_TYPE_INT, 1);
    const CheckActionResult effect = check_test_action(test, table, divide);
    check_test_expect(test, (effect.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 &&
                           !check_action_integer_constant(&effect.value, divide.extensions),
                      "a known GNU folded result never hides an evaluated side effect from constant-context validation");
    const TypeId complex_type = type_complex(table, BUSTER_TYPE_INT);
    const CheckAction complex = { .op = CHECK_OP_MULTIPLY,
                                 .operands = { check_test_integer_complex_value(complex_type, 50000, 0),
                                               check_test_integer_complex_value(complex_type, 50000, 0) },
                                 .extensions = BUSTER_CHECK_EXTENSION_GNU_CONSTANT, .node = 218 };
    const CheckActionResult product = check_test_action(test, table, complex);
    check_test_expect(test, product.value.fault == CHECK_FAULT_NONE && product.value.constant_kind == CHECK_CONSTANT_COMPLEX &&
                           product.value.integer == 2500000000u && product.warnings == 0 &&
                           (product.value.flags & (BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM)) == BUSTER_CHECK_GNU_CONSTANT,
                      "integer-complex intermediate overflow uses the explicit policy without pretending to be lexical ICE");
}

static void check_test_integer_policy_graph(CheckTest* test, TypeTable* table)
{
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(131072);
    Arena scratch = arena_reserve(65536);
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, &storage, 16, 0);
    check_test_expect(test, ready, "GNU integer context graph allocation");
    if (ready)
    {
        const uint32_t maximum = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, INT32_MAX));
        const uint32_t one = check_test_leaf(&graph, 2, check_test_integer(BUSTER_TYPE_INT, 1));
        const uint32_t zero = check_test_leaf(&graph, 3, check_test_integer(BUSTER_TYPE_INT, 0));
        const uint32_t sum = check_graph_add(&graph, CHECK_OP_ADD, 4, maximum, one, BUSTER_CHECK_NONE);
        const uint32_t negative = check_graph_add(&graph, CHECK_OP_LESS, 5, sum, zero, BUSTER_CHECK_NONE);
        const uint32_t strict = check_graph_add(&graph, CHECK_OP_SELECT, 6, negative, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        graph.contexts[negative] = BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_GNU_CONSTANT;
        graph.contexts[strict] = BUSTER_CHECK_CONTEXT_ICE;
        for (uint32_t policy = 0; policy < 2; policy += 1)
        {
            graph.extensions = policy != 0 ? BUSTER_CHECK_EXTENSION_GNU_CONSTANT : 0;
            CheckStream scalar;
            const CheckOptions scalar_options = { .scalar = true };
            const bool checked = check_graph_run(table, &graph, &output, &scratch, &scalar_options, &scalar);
            check_test_expect(test, checked && scalar.report_count == (policy != 0 ? 1 : 2),
                              "explicit graph fold policy differs from each root's strict or GNU acceptance policy");
            if (checked)
            {
                check_test_expect(test, policy != 0 ? scalar.reports[0].node == strict && scalar.reports[0].message == CHECK_MESSAGE_ICE &&
                                                     scalar.warnings[sum] == CHECK_WARNING_INTEGER_OVERFLOW && scalar.warnings[negative] == CHECK_WARNING_GNU_CONSTANT &&
                                                     scalar.warning_ignored[negative] == CHECK_WARNING_GNU_CONSTANT :
                                                     scalar.reports[0].fault == CHECK_FAULT_SIGNED_OVERFLOW && scalar.warnings[sum] == 0,
                                  "graph retains exact operation warning provenance without widening a strict ICE root");
                const uint64_t mark = output.length;
                for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
                {
                    CheckStream vector;
                    const CheckOptions vector_options = { .batch_size = batch };
                    check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &vector_options, &vector) &&
                                           check_test_stream_equal(&scalar, &vector, 0),
                                      "GNU extension graph warnings, witnesses and strict reports agree at every scheduler partition");
                    arena_pop_to(&output, mark);
                }
            }
            arena_pop_to(&output, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
}

static void check_test_integer_warning_contexts(CheckTest* test, TypeTable* table, const char* clang)
{
    const struct
    {
        CheckOp op;
        uint32_t left;
        uint32_t right;
        uint32_t warning;
        const char* expression;
        const char* group;
    } cases[] = {
        { CHECK_OP_ADD, INT32_MAX, 1, CHECK_WARNING_INTEGER_OVERFLOW, "2147483647+1", "-Winteger-overflow" },
        { CHECK_OP_SHIFT_LEFT, 1, UINT32_MAX, CHECK_WARNING_SHIFT_COUNT_NEGATIVE, "1<<-1", "-Wshift-count-negative" },
        { CHECK_OP_SHIFT_LEFT, 1, 32, CHECK_WARNING_SHIFT_COUNT_OVERFLOW, "1<<32", "-Wshift-count-overflow" },
        { CHECK_OP_SHIFT_LEFT, UINT32_MAX, 1, CHECK_WARNING_SHIFT_NEGATIVE_VALUE, "(-1)<<1", "-Wshift-negative-value" },
        { CHECK_OP_SHIFT_LEFT, 3, 31, CHECK_WARNING_SHIFT_OVERFLOW, "3<<31", "-Wshift-overflow" },
    };
    const char* contexts[] = { "int f(void){return (%s);}\n", "enum { value=(%s) };\n", "int f(void){return sizeof(%s);}\n",
                               "int f(void){return 1?0:(%s);}\n", "int f(void){return 0&&(%s);}\n",
                               "int f(void){return __builtin_constant_p(%s);}\n", "static int value=(%s);\n",
                               "int f(void){return __builtin_choose_expr(1,0,(%s));}\n",
                               "int f(void){return _Generic((%s),int:0,default:1);}\n",
                               "_Static_assert(1||(%s),\"discarded overflow is strict ICE\");\n" };
    char directory[] = "/tmp/buster-checker-integer-warnings-XXXXXX";
    const bool directory_ready = mkdtemp(directory) != NULL;
    char source_path[160];
    char output_path[160];
    char error_path[160];
    snprintf(source_path, sizeof(source_path), "%s/source.c", directory);
    snprintf(output_path, sizeof(output_path), "%s/output", directory);
    snprintf(error_path, sizeof(error_path), "%s/errors", directory);
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(65536);
    Arena scratch = arena_reserve(65536);
    check_test_expect(test, directory_ready && storage.base != NULL && output.base != NULL && scratch.base != NULL,
                      "integer warning context oracle allocation");
    for (uint32_t context = 0; directory_ready && context < BUSTER_ARRAY_LENGTH(contexts); context += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
        {
            const uint32_t primary = context == 0 || context == 5 || context == 6 ? cases[i].warning :
                                     context == 7 ? (i != 0 ? cases[i].warning : 0) :
                                     context == 1 && i == 0 ? CHECK_WARNING_INTEGER_OVERFLOW : i == 4 ? CHECK_WARNING_SHIFT_OVERFLOW : 0;
            char body[256];
            char source[384];
            snprintf(body, sizeof(body), contexts[context], cases[i].expression);
            snprintf(source, sizeof(source), "#pragma clang diagnostic error \"%s\"\n%s", cases[i].group, body);
            char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", source_path, NULL };
            const bool written = parse_test_write(source_path, source, strlen(source));
            const int status = written ? object_test_run(arguments, output_path, error_path) : -1;
            check_test_expect(test, written && status >= 0 && (status != 0) == (primary != 0),
                              "Clang independently qualifies exact warning groups in evaluated, required and discarded contexts");
            CheckGraph graph;
            const bool ready = check_graph_initialize(&graph, &storage, 8, 1);
            check_test_expect(test, ready, "integer warning context graph allocation");
            if (ready)
            {
                uint8_t suppressed[8] = { 0 };
                graph.extensions = BUSTER_CHECK_EXTENSION_GNU_CONSTANT;
                graph.warning_suppressed = context == 7 || context == 8 ? suppressed : NULL;
                const uint32_t left = check_test_leaf(&graph, 1, check_test_integer(BUSTER_TYPE_INT, cases[i].left));
                const uint32_t right = check_test_leaf(&graph, 2, check_test_integer(BUSTER_TYPE_INT, cases[i].right));
                const uint32_t operation = check_graph_add(&graph, cases[i].op, 3, left, right, BUSTER_CHECK_NONE);
                if (context == 1)
                {
                    graph.contexts[operation] = BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_GNU_CONSTANT;
                }
                else if (context == 2)
                {
                    check_graph_add(&graph, CHECK_OP_SIZEOF, 4, operation, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                }
                else if (context == 3 || context == 4)
                {
                    const uint32_t condition = check_test_leaf(&graph, 4, check_test_integer(BUSTER_TYPE_INT, context == 3));
                    check_graph_add(&graph, context == 3 ? CHECK_OP_CONDITIONAL : CHECK_OP_LOGICAL_AND, 5,
                                    condition, context == 3 ? condition : operation, context == 3 ? operation : BUSTER_CHECK_NONE);
                }
                else if (context == 5)
                {
                    check_test_special_node(&graph, CHECK_OP_BUILTIN_QUERY, CBUILTIN_CONSTANT_P, &operation, 1);
                }
                else if (context == 6)
                {
                    graph.contexts[operation] = BUSTER_CHECK_CONTEXT_STATIC;
                }
                else if (context == 7 || context == 8)
                {
                    const uint32_t selected = check_test_leaf(&graph, 4, check_test_integer(BUSTER_TYPE_INT, 0));
                    check_graph_add(&graph, CHECK_OP_SELECT, 5, selected, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
                    suppressed[operation] = CHECK_WARNING_INTEGER_OVERFLOW |
                                            (context == 8 ? CHECK_WARNING_SHIFT_COUNT_NEGATIVE | CHECK_WARNING_SHIFT_COUNT_OVERFLOW |
                                                            CHECK_WARNING_SHIFT_NEGATIVE_VALUE : 0);
                }
                else if (context == 9)
                {
                    const uint32_t condition = check_test_leaf(&graph, 4, check_test_integer(BUSTER_TYPE_INT, 1));
                    const uint32_t assertion = check_graph_add(&graph, CHECK_OP_LOGICAL_OR, 5, condition, operation, BUSTER_CHECK_NONE);
                    graph.contexts[assertion] = BUSTER_CHECK_CONTEXT_ICE | BUSTER_CHECK_CONTEXT_ASSERT | BUSTER_CHECK_CONTEXT_GNU_CONSTANT;
                }
                const uint32_t generic = context == 1 ? CHECK_WARNING_GNU_CONSTANT : 0;
                CheckStream scalar;
                const CheckOptions scalar_options = { .scalar = true };
                const bool checked = check_graph_run(table, &graph, &output, &scratch, &scalar_options, &scalar);
                check_test_expect(test, checked && scalar.report_count == 0 && scalar.warnings[operation] == (primary | generic) &&
                                       scalar.warning_ignored[operation] == generic,
                                  "graph warning facts and default-ignored GNU context match the independent language oracle");
                if (checked)
                {
                    const uint64_t mark = output.length;
                    for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
                    {
                        CheckStream vector;
                        const CheckOptions options = { .batch_size = batch };
                        check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &options, &vector) &&
                                               check_test_stream_equal(&scalar, &vector, graph.argument_count),
                                          "warning contexts retain exact metadata in every scalar/vector partition");
                        arena_pop_to(&output, mark);
                    }
                    if (context == 9)
                    {
                        check_test_expect(test, scalar.warnings[graph.count - 1] == 0 &&
                                               (check_stream_flags(&scalar, graph.count - 1) & BUSTER_CHECK_ICE) != 0,
                                          "discarded wrapped lexical ICE retains strict eligibility without a GNU folding warning");
                    }
                    if (context == 7 || context == 8)
                    {
                        suppressed[operation] = CHECK_WARNING_SHIFT_OVERFLOW;
                        CheckStream invalid;
                        check_test_expect(test, !check_graph_run(table, &graph, &output, &scratch, &scalar_options, &invalid) &&
                                               invalid.status == CHECK_STATUS_INVALID_GRAPH && output.length == mark && scratch.length == 0,
                                          "source warning suppression cannot hide retained shift-overflow diagnostics or publish partial output");
                    }
                }
            }
            arena_pop_to(&storage, 0);
            arena_pop_to(&output, 0);
        }
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    unlink(error_path);
    unlink(output_path);
    unlink(source_path);
    rmdir(directory);
}

static __attribute__((noinline)) void check_test_integer_policy(CheckTest* test, TypeTable* table, const char* clang)
{
    check_test_integer_policy_actions(test, table);
    check_test_integer_policy_graph(test, table);
    check_test_integer_warning_contexts(test, table, clang);
}

#endif
