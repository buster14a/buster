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

// Clang 23's generic vector builtins past the 23 observed ones, as source: each fixture's acceptance by clang, then by
// both paths of the checker, with clang's first error's line. Each a translation unit of its own, valid calls of each
// family at each kind of operand, then one error each: an operand of another type, kind or lanes, a missing or
// extra one, copysign's GNU and ext_vector_type operands (its operands' types compared whole), ldexp's exponents, a
// reduction's start value, the masked builtins' masks, pointers and passthru vectors, and the matrix builtins, which
// every call is an error of without -fenable-matrix.
static void check_test_builtin_vector_added_sources(CheckTest* test, const char* clang)
{
    static const char types[] =
        "typedef float vf __attribute__((vector_size(16)));typedef float ef __attribute__((ext_vector_type(4)));"
        "typedef double vd __attribute__((vector_size(32)));typedef int vi __attribute__((vector_size(16)));"
        "typedef unsigned vu __attribute__((vector_size(16)));typedef int ei __attribute__((ext_vector_type(4)));"
        "typedef long long vl __attribute__((vector_size(32)));typedef short vs __attribute__((vector_size(8)));"
        "typedef _Bool b4 __attribute__((ext_vector_type(4)));typedef _Bool b8 __attribute__((ext_vector_type(8)));"
        "typedef float e8 __attribute__((ext_vector_type(8)));struct S{int x;};\n";
    static const struct
    {
        const char* source;
        bool rejected;
    } fixtures[] = {
        { "void f(vf a,vd d,float x,double y,long double l,_Float16 h,__bf16 b){(void)__builtin_elementwise_acos(a);(void)__builtin_elementwise_asin(d);"
          "(void)__builtin_elementwise_atan(x);(void)__builtin_elementwise_cos(y);(void)__builtin_elementwise_cosh(l);(void)__builtin_elementwise_exp(h);"
          "(void)__builtin_elementwise_exp10(b);(void)__builtin_elementwise_exp2(a);(void)__builtin_elementwise_log(d);(void)__builtin_elementwise_log10(x);"
          "(void)__builtin_elementwise_log2(y);(void)__builtin_elementwise_sin(l);(void)__builtin_elementwise_sinh(a);(void)__builtin_elementwise_tan(d);"
          "(void)__builtin_elementwise_tanh(x);(void)__builtin_elementwise_ceil(y);(void)__builtin_elementwise_floor(l);(void)__builtin_elementwise_trunc(h);"
          "(void)__builtin_elementwise_rint(a);(void)__builtin_elementwise_nearbyint(d);(void)__builtin_elementwise_roundeven(x);"
          "(void)__builtin_elementwise_round(y);(void)__builtin_elementwise_canonicalize(l);}\n",
          false },
        { "void f(vf a,ef e,vd d,float x,long double l){(void)__builtin_elementwise_atan2(a,a);(void)__builtin_elementwise_fmod(d,d);"
          "(void)__builtin_elementwise_pow(x,x);(void)__builtin_elementwise_copysign(l,l);(void)__builtin_elementwise_maximum(a,e);"
          "(void)__builtin_elementwise_minimum(e,a);(void)__builtin_elementwise_maxnum(d,d);(void)__builtin_elementwise_minnum(x,x);"
          "(void)__builtin_elementwise_maximumnum(a,a);(void)__builtin_elementwise_minimumnum(l,l);}\n",
          false },
        // The results' types, from parameters: the checker refuses a compound literal inside __typeof__ (a gap of its own).
        { "void f(vf a,vd d,vi i){_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_sin(a)),vf),\"vector\");"
          "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_pow(1.0f,2.0f)),float),\"scalar\");"
          "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_elementwise_ldexp(d,i)),vd),\"ldexp\");"
          "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_reduce_maximum(d)),double),\"reduce\");"
          "_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_reduce_assoc_fadd(a,2.0)),float),\"start\");}\n",
          false },
        { "void f(int n){(void)__builtin_elementwise_sin(n);}\n", true },
        { "void f(vi a){(void)__builtin_elementwise_floor(a);}\n", true },
        { "void f(vf a){(void)__builtin_elementwise_canonicalize(a,a);}\n", true },
        { "void f(vf a){(void)__builtin_elementwise_pow(a);}\n", true },
        { "void f(float x,double y){(void)__builtin_elementwise_atan2(x,y);}\n", true },
        { "void f(vf a,ef e){(void)__builtin_elementwise_copysign(a,e);}\n", true },
        { "void f(vf a,float x){(void)__builtin_elementwise_maxnum(a,x);}\n", true },
        { "void f(vi a,vi b){(void)__builtin_elementwise_minimum(a,b);}\n", true },
        { "void f(vf a,vi i,vl l,ei e,float x,int n,short s,unsigned u,_Bool b,__int128 w){(void)__builtin_elementwise_ldexp(a,i);"
          "(void)__builtin_elementwise_ldexp(a,l);(void)__builtin_elementwise_ldexp(a,e);(void)__builtin_elementwise_ldexp(x,n);"
          "(void)__builtin_elementwise_ldexp(x,s);(void)__builtin_elementwise_ldexp(x,u);(void)__builtin_elementwise_ldexp(x,b);"
          "(void)__builtin_elementwise_ldexp(x,w);}\n",
          false },
        { "void f(vf a,float x){(void)__builtin_elementwise_ldexp(a,x);}\n", true },
        { "void f(vf a,int n){(void)__builtin_elementwise_ldexp(a,n);}\n", true },
        { "void f(float x,vi i){(void)__builtin_elementwise_ldexp(x,i);}\n", true },
        { "void f(vf a,vs s){(void)__builtin_elementwise_ldexp(a,s);}\n", false },
        { "void f(vd d,vs s){(void)__builtin_elementwise_ldexp(d,(vl){0});(void)__builtin_elementwise_ldexp(d,s);}\n", false },
        { "void f(int n,int m){(void)__builtin_elementwise_ldexp(n,m);}\n", true },
        { "void f(vu a,vi i,ei e,int n,_Bool b){(void)__builtin_elementwise_bitreverse(a);(void)__builtin_elementwise_ctzg(i);"
          "(void)__builtin_elementwise_ctzg(i,e);(void)__builtin_elementwise_ctzg(n,n);(void)__builtin_elementwise_clmul(a,a);"
          "(void)__builtin_elementwise_pdep(i,e);(void)__builtin_elementwise_pext(b,b);}\n",
          false },
        { "void f(vf a){(void)__builtin_elementwise_ctzg(a);}\n", true },
        { "void f(vi a){(void)__builtin_elementwise_ctzg(a,1);}\n", true },
        { "void f(vu a){(void)__builtin_elementwise_ctzg(a,a,a);}\n", true },
        { "void f(vi a,vu b){(void)__builtin_elementwise_clmul(a,b);}\n", true },
        { "void f(int n,unsigned m){(void)__builtin_elementwise_pdep(n,m);}\n", true },
        { "void f(float x){(void)__builtin_elementwise_pext(x,x);}\n", true },
        { "void f(vi a,b4 m,vf v,vd d){(void)__builtin_reduce_xor(a);(void)__builtin_reduce_xor(m);(void)__builtin_reduce_maximum(v);"
          "(void)__builtin_reduce_minimum(d);(void)__builtin_reduce_assoc_fadd(v);(void)__builtin_reduce_assoc_fadd(d,(long double)1);"
          "(void)__builtin_reduce_in_order_fadd(v,(_Float16)1);}\n",
          false },
        { "void f(vf a){(void)__builtin_reduce_xor(a);}\n", true },
        { "void f(vi a){(void)__builtin_reduce_maximum(a);}\n", true },
        { "void f(float x){(void)__builtin_reduce_minimum(x);}\n", true },
        { "void f(vf a){(void)__builtin_reduce_assoc_fadd(a,1);}\n", true },
        { "void f(vf a){(void)__builtin_reduce_assoc_fadd(a,1.0f,2.0f);}\n", true },
        { "void f(vf a){(void)__builtin_reduce_in_order_fadd(a);}\n", true },
        { "void f(vf a){(void)__builtin_reduce_in_order_fadd(a,a);}\n", true },
        { "void f(b4 m,float*p,const float*c,ef e,vf v,ei i,vi j,b8 w){(void)__builtin_masked_load(m,p);(void)__builtin_masked_load(m,c,e);"
          "(void)__builtin_masked_expand_load(m,p);__builtin_masked_store(m,v,p);__builtin_masked_compress_store(m,e,p);"
          "(void)__builtin_masked_gather(m,i,p);(void)__builtin_masked_gather(m,j,c,e);__builtin_masked_scatter(m,j,e,p);}\n",
          false },
        { "void f(vi m,float*p){(void)__builtin_masked_load(m,p);}\n", true },
        { "void f(_Bool m,float*p){(void)__builtin_masked_load(m,p);}\n", true },
        { "void f(b4 m,ef*p){(void)__builtin_masked_load(m,p);}\n", true },
        { "void f(b4 m,int x){(void)__builtin_masked_load(m,x);}\n", true },
        { "void f(b4 m,volatile float*p){(void)__builtin_masked_load(m,p);}\n", true },
        { "void f(b4 m,float*p,vf v){(void)__builtin_masked_load(m,p,v);}\n", true },
        { "void f(b4 m,float*p,e8 v){(void)__builtin_masked_load(m,p,v);}\n", true },
        { "void f(b4 m,float*p){(void)__builtin_masked_load(m);}\n", true },
        { "void f(b4 m,const float*p,ef v){__builtin_masked_store(m,v,p);}\n", true },
        { "void f(b4 m,int*p,ef v){__builtin_masked_store(m,v,p);}\n", true },
        { "void f(b4 m,float*p,e8 v){__builtin_masked_store(m,v,p);}\n", true },
        { "void f(b4 m,float*p,float v){__builtin_masked_compress_store(m,v,p);}\n", true },
        { "void f(b4 m,float*p,ef i){(void)__builtin_masked_gather(m,i,p);}\n", true },
        { "void f(b8 m,float*p,ei i,ef v){__builtin_masked_scatter(m,i,v,p);}\n", true },
        { "void f(b4 m,_Atomic(float)*p){(void)__builtin_masked_load(m,p);}\n", true },
        { "void f(float x){(void)__builtin_matrix_transpose(x);}\n", true },
        { "void f(float*p){(void)__builtin_matrix_column_major_load(p,2,2,2);}\n", true },
        { "void f(float*p,float x){__builtin_matrix_column_major_store(x,p,2);}\n", true },
        { "void f(void){(void)__builtin_matrix_transpose();}\n", true },
    };
    char directory[] = "/tmp/buster-check-vector-added-XXXXXX";
    bool ready = mkdtemp(directory) != NULL;
    char paths[BUSTER_ARRAY_LENGTH(fixtures)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(fixtures)];
    char* sources[BUSTER_ARRAY_LENGTH(fixtures)] = { 0 };
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(fixtures)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const size_t length = sizeof(types) + strlen(fixtures[i].source);
        sources[i] = malloc(length);
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        ready = ready && sources[i] != NULL;
        if (ready)
        {
            snprintf(sources[i], length, "%s%s", types, fixtures[i].source);
            ready = parse_test_write(paths[i], sources[i], strlen(sources[i]));
        }
    }
    const char* mode = "-std=gnu17";
    const bool checked = ready && parse_test_clang(clang, &mode, 1, pointers, BUSTER_ARRAY_LENGTH(fixtures), outcomes);
    check_test_expect(test, checked, "the added vector builtins' source oracle runs");
    for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        char name[96];
        snprintf(name, sizeof(name), "added vector builtin source %u", i);
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, name);
        const CheckSourceFixture fixture = { .name = name, .source = sources[i], .rejected = fixtures[i].rejected };
        check_test_source(test, &fixture, paths[i], outcomes + i);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        unlink(paths[i]);
        free(sources[i]);
    }
    rmdir(directory);
}

static __attribute__((noinline)) void check_test_builtin_vector_graph(CheckTest* test, TypeTable* table, const char* clang)
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
            const CheckValue node_value = check_stream_value(&scalar, nodes[i]);
            const CheckValue* value = &node_value;
            check_test_expect(test, effect->node == nodes[i] && value->type == fixtures[i].expected &&
                                   value->constant_kind == CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
                                   effect->runtime_builtin == fixtures[i].id && effect->evaluated_argument_count == fixtures[i].evaluated,
                              "vector builtin retains independently specified result type and evaluated prefix");
        }
        if (checked)
        {
            check_test_expect(test, scalar.vectors[4].reads_fp_environment && scalar.vectors[4].may_raise_fp_exceptions &&
                                   scalar.vectors[19].reads_fp_environment && scalar.vectors[19].may_raise_fp_exceptions &&
                                   scalar.vectors[20].nondeterministic && (check_stream_flags(&scalar, nodes[20]) & BUSTER_CHECK_FP_ENVIRONMENT) == 0 &&
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
        check_test_graph_cut(&small, nodes[0] + 1);
        small.argument_count = 1;
        check_test_special_graph_bounds(test, table, &small);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&storage);
    check_test_builtin_vector_oracles(test, clang);
    check_test_builtin_vector_added_sources(test, clang);
}

#endif
