// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Exact vector builtin type/effect contracts, independent Clang mutations and bounded graph publication.
#ifndef BUSTER_C_CHECK_BUILTIN_VECTOR_GRAPH_TEST_INCLUDED
#define BUSTER_C_CHECK_BUILTIN_VECTOR_GRAPH_TEST_INCLUDED

static void check_test_builtin_vector_oracles(CheckTest* test, const char* clang)
{
    const char* sources[] = {
        "typedef int I __attribute__((ext_vector_type(4)));typedef unsigned U __attribute__((ext_vector_type(4)));"
        "typedef float F __attribute__((ext_vector_type(4)));\n"
        "#define S(e,t) _Static_assert(__builtin_types_compatible_p(__typeof__(e),t),#e)\n"
        "void f(I i,U u,F x,int*p){"
        "S(__builtin_elementwise_abs(i),I);S(__builtin_elementwise_add_sat(i,i),I);"
        "S(__builtin_elementwise_bitreverse(u),U);S(__builtin_elementwise_clzg(u),U);"
        "S(__builtin_elementwise_fma(x,x,x),F);S(__builtin_elementwise_fshl(u,u,u),U);"
        "S(__builtin_elementwise_fshr(u,u,u),U);S(__builtin_elementwise_max(i,i),I);"
        "S(__builtin_elementwise_min(i,i),I);S(__builtin_elementwise_popcount(u),U);"
        "S(__builtin_elementwise_sqrt(x),F);S(__builtin_elementwise_sub_sat(i,i),I);"
        "S(__builtin_reduce_add(i),int);S(__builtin_reduce_and(u),unsigned);"
        "S(__builtin_reduce_max(i),int);S(__builtin_reduce_min(x),float);"
        "S(__builtin_reduce_mul(i),int);S(__builtin_reduce_or(u),unsigned);"
        "S(__builtin_shufflevector(i,i,0,3,4,7),I);S(__builtin_convertvector(i,F),F);"
        "S(__builtin_nondeterministic_value(i),I);S(__builtin_nontemporal_load(p),int);"
        "S(__builtin_nontemporal_store(7,p),void);}\n",
        "typedef unsigned U __attribute__((ext_vector_type(4)));U f(U x){return __builtin_elementwise_abs(x);}\n",
        "typedef int I __attribute__((ext_vector_type(4)));I f(I x){return __builtin_elementwise_fma(x,x,x);}\n",
        "typedef float F __attribute__((ext_vector_type(4)));float f(F x){return __builtin_reduce_add(x);}\n",
        "typedef int I __attribute__((ext_vector_type(4)));I f(I x){return __builtin_shufflevector(x,x,0,1,2,8);}\n",
        "typedef int I __attribute__((ext_vector_type(4)));typedef float F __attribute__((ext_vector_type(2)));F f(I x){return __builtin_convertvector(x,F);}\n",
        "struct S{int x;};struct S f(struct S x){return __builtin_nondeterministic_value(x);}\n",
        "void f(void*p){__builtin_nontemporal_store(7,p);}\n",
        "int f(void){return __builtin_elementwise_abs();}\n",
        "typedef int I __attribute__((ext_vector_type(4)));I f(I x){return __builtin_elementwise_abs(x,x);}\n",
    };
    char directory[] = "/tmp/buster-check-vector-builtins-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(sources)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(sources)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(sources)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && parse_test_write(paths[i], sources[i], strlen(sources[i]));
    }
    const char* modes[] = { "-std=gnu17", "-std=gnu23" };
    for (uint32_t mode = 0; ready && mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
    {
        const bool checked = parse_test_clang(clang, modes + mode, 1, pointers, BUSTER_ARRAY_LENGTH(sources), outcomes);
        check_test_expect(test, checked, "independent vector builtin type and constraint oracle runs");
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(sources); i += 1)
        {
            char description[112];
            snprintf(description, sizeof(description), "vector builtin type or constraint mutation %u in %s", i, modes[mode]);
            check_test_expect(test, outcomes[i].rejected == (i != 0), description);
        }
    }
    check_test_expect(test, ready, "vector builtin oracle fixtures are created");
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sources); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

static void check_test_builtin_vector_graph(CheckTest* test, TypeTable* table, const char* clang)
{
    const TypeId vi = type_vector(table, BUSTER_TYPE_INT, 4, true);
    const TypeId vu = type_vector(table, BUSTER_TYPE_UINT, 4, true);
    const TypeId vf = type_vector(table, BUSTER_TYPE_FLOAT, 4, true);
    const TypeId ip = type_pointer(table, BUSTER_TYPE_INT);
    struct VectorBuiltinFixture
    {
        CBuiltinId id;
        TypeId arguments[6];
        TypeId destination;
        TypeId expected;
        uint32_t count;
        uint32_t evaluated;
    };
    const struct VectorBuiltinFixture fixtures[] = {
        { CBUILTIN_ELEMENTWISE_ABS, { vi }, 0, vi, 1, 1 },
        { CBUILTIN_ELEMENTWISE_ADD_SAT, { vi, vi }, 0, vi, 2, 2 },
        { CBUILTIN_ELEMENTWISE_BITREVERSE, { vu }, 0, vu, 1, 1 },
        { CBUILTIN_ELEMENTWISE_CLZG, { vu }, 0, vu, 1, 1 },
        { CBUILTIN_ELEMENTWISE_FMA, { vf, vf, vf }, 0, vf, 3, 3 },
        { CBUILTIN_ELEMENTWISE_FSHL, { vu, vu, vu }, 0, vu, 3, 3 },
        { CBUILTIN_ELEMENTWISE_FSHR, { vu, vu, vu }, 0, vu, 3, 3 },
        { CBUILTIN_ELEMENTWISE_MAX, { vi, vi }, 0, vi, 2, 2 },
        { CBUILTIN_ELEMENTWISE_MIN, { vi, vi }, 0, vi, 2, 2 },
        { CBUILTIN_ELEMENTWISE_POPCOUNT, { vu }, 0, vu, 1, 1 },
        { CBUILTIN_ELEMENTWISE_SQRT, { vf }, 0, vf, 1, 1 },
        { CBUILTIN_ELEMENTWISE_SUB_SAT, { vi, vi }, 0, vi, 2, 2 },
        { CBUILTIN_REDUCE_ADD, { vi }, 0, BUSTER_TYPE_INT, 1, 1 },
        { CBUILTIN_REDUCE_AND, { vu }, 0, BUSTER_TYPE_UINT, 1, 1 },
        { CBUILTIN_REDUCE_MAX, { vi }, 0, BUSTER_TYPE_INT, 1, 1 },
        { CBUILTIN_REDUCE_MIN, { vf }, 0, BUSTER_TYPE_FLOAT, 1, 1 },
        { CBUILTIN_REDUCE_MUL, { vi }, 0, BUSTER_TYPE_INT, 1, 1 },
        { CBUILTIN_REDUCE_OR, { vu }, 0, BUSTER_TYPE_UINT, 1, 1 },
        { CBUILTIN_SHUFFLEVECTOR, { vi, vi, BUSTER_TYPE_INT, BUSTER_TYPE_INT, BUSTER_TYPE_INT, BUSTER_TYPE_INT }, 0, vi, 6, 2 },
        { CBUILTIN_CONVERTVECTOR, { vi }, vf, vf, 1, 1 },
        { CBUILTIN_NONDETERMINISTIC_VALUE, { vi }, 0, vi, 1, 0 },
        { CBUILTIN_NONTEMPORAL_LOAD, { ip }, 0, BUSTER_TYPE_INT, 1, 1 },
        { CBUILTIN_NONTEMPORAL_STORE, { BUSTER_TYPE_INT, ip }, 0, BUSTER_TYPE_VOID, 2, 2 },
    };
    Arena storage = arena_reserve(65536);
    Arena output = arena_reserve(131072);
    Arena scratch = arena_reserve(65536);
    CheckGraph graph;
    const bool ready = check_graph_initialize(&graph, &storage, 128, 128);
    check_test_expect(test, ready, "all vector builtin graph fixture storage");
    uint32_t nodes[BUSTER_ARRAY_LENGTH(fixtures)] = { 0 };
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        uint32_t arguments[6];
        for (uint32_t j = 0; j < fixtures[i].count; j += 1)
        {
            CheckValue value = { .type = fixtures[i].arguments[j], .category = CHECK_RVALUE };
            if (fixtures[i].id == CBUILTIN_SHUFFLEVECTOR && j >= 2)
            {
                const uint32_t indices[] = { 0, 3, 4, 7 };
                value.flags = BUSTER_CHECK_ICE;
                value.constant_kind = CHECK_CONSTANT_INTEGER;
                value.integer = indices[j - 2];
            }
            if (fixtures[i].id == CBUILTIN_NONDETERMINISTIC_VALUE)
            {
                value.flags = BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT;
                value.fault = CHECK_FAULT_DIVIDE_BY_ZERO;
                value.fault_node = graph.count;
            }
            arguments[j] = check_test_leaf(&graph, graph.count + 1, value);
        }
        nodes[i] = check_test_special_node(&graph, CHECK_OP_BUILTIN_VECTOR, fixtures[i].id, arguments, fixtures[i].count);
        graph.types[nodes[i]] = fixtures[i].destination;
        check_test_expect(test, check_graph_builtin_op(fixtures[i].id) == CHECK_OP_BUILTIN_VECTOR,
                          "every exact vector catalog ID routes to its semantic family");
    }
    if (ready)
    {
        const CheckOptions options = { .scalar = true };
        CheckStream scalar;
        const bool checked = check_graph_run(table, &graph, &output, &scratch, &options, &scalar);
        check_test_expect(test, checked && scalar.report_count == 0 && scalar.vector_count == BUSTER_ARRAY_LENGTH(fixtures),
                          "all vector builtin actions publish one compact descriptor in source order");
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
        {
            const CheckBuiltinVectorEffect* effect = scalar.vectors + i;
            const CheckValue* value = scalar.values + nodes[i];
            check_test_expect(test, effect->node == nodes[i] && value->type == fixtures[i].expected &&
                                   value->constant_kind == CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
                                   effect->runtime_builtin == fixtures[i].id && effect->evaluated_argument_count == fixtures[i].evaluated,
                              "vector builtin retains independently specified result type and evaluated prefix");
        }
        if (checked)
        {
            check_test_expect(test, scalar.vectors[4].reads_fp_environment && scalar.vectors[4].may_raise_fp_exceptions &&
                                   scalar.vectors[19].reads_fp_environment && scalar.vectors[19].may_raise_fp_exceptions &&
                                   scalar.vectors[20].nondeterministic && (scalar.values[nodes[20]].flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0 &&
                                   scalar.vectors[21].reads_memory && scalar.vectors[22].writes_memory &&
                                   scalar.vectors[22].memory_type == BUSTER_TYPE_INT,
                              "floating conversion, nondeterminism and nontemporal memory effects remain explicit");
            const uint64_t mark = output.length;
            for (uint32_t batch = 1; batch <= BUSTER_CHECK_BATCH; batch += 1)
            {
                CheckStream actual;
                const CheckOptions partition = { .batch_size = batch };
                check_test_expect(test, check_graph_run(table, &graph, &output, &scratch, &partition, &actual) &&
                                       check_test_stream_equal(&scalar, &actual, graph.argument_count),
                                  "all vector graph partitions preserve complete builtin descriptors");
                arena_pop_to(&output, mark);
            }
        }
        CheckGraph small = graph;
        small.count = nodes[0] + 1;
        small.argument_count = 1;
        check_test_special_graph_bounds(test, table, &small);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    check_test_builtin_vector_oracles(test, clang);
}

#endif
