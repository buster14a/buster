// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original transparent-union type, source-history and resource tests. The handwritten sources are checked by
// installed Clang; direct tests inspect nominal identity and composites that acceptance alone cannot establish.
#ifndef BUSTER_CHECK_TRANSPARENT_TYPE_TEST_IMPLEMENTATION
#define BUSTER_CHECK_TRANSPARENT_TYPE_TEST_IMPLEMENTATION 1

static TypeId check_test_transparent_record(CheckTest* test, TypeTable* table, const TypeMember* members, uint32_t count, TypeTransparentUnionStatus expected)
{
    const TypeId record = type_tag(table, BUSTER_TYPE_KIND_UNION, 133901);
    const bool complete = type_complete_record(table, record, members, count, (TypeRecordAttributes){ 0 });
    const TypeError error = table->last_error;
    const TypeTransparentUnionResult marked = type_mark_transparent_union(table, record);
    const uint32_t member = expected == TYPE_TRANSPARENT_UNION_FIRST_FLOAT || expected == TYPE_TRANSPARENT_UNION_FIRST_VECTOR ? 0 :
                            expected == TYPE_TRANSPARENT_UNION_SIZE || expected == TYPE_TRANSPARENT_UNION_ALIGNMENT ? 1 : UINT32_MAX;
    check_test_expect(test, complete && marked.status == expected && marked.member == member && table->last_error == error,
                      "transparent validator reports the exact subject constraint without setting a type-construction error");
    check_test_expect(test, type_is_transparent_union(table, record) == (expected == TYPE_TRANSPARENT_UNION_APPLIED),
                      "only validated transparent unions acquire the nominal flag");
    return record;
}

static void check_test_transparent_relation(CheckTest* test, TypeTable* table, TypeId left, TypeId right, TypeId expected)
{
    for (uint32_t order = 0; order < 2; order += 1)
    {
        const TypeId a = order == 0 ? left : right;
        const TypeId b = order == 0 ? right : left;
        const TypeAnswer answer = type_compatible(table, a, b);
        const TypeId composite = type_composite(table, a, b);
        check_test_expect(test, answer == (expected != 0 ? TYPE_ANSWER_YES : TYPE_ANSWER_NO) && composite == expected,
                          "transparent compatibility and visible composite agree in both declaration orders");
    }
}

static void check_test_transparent_types(CheckTest* test, const char* clang)
{
    TypeTestTable storage;
    const bool ready = type_test_table_open(&storage, 8192);
    check_test_expect(test, ready, "transparent type fixture storage initializes");
    if (ready)
    {
        TypeTable* table = &storage.table;
        const TypeId ip = type_pointer(table, BUSTER_TYPE_INT);
        const TypeId dp = type_pointer(table, BUSTER_TYPE_DOUBLE);
        const TypeId fp = type_pointer(table, BUSTER_TYPE_FLOAT);
        TypeMember members[3] = {
            { .name = 1, .type = ip, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
            { .name = 2, .type = dp, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
            { .name = 3, .type = fp, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        };
        const TypeId record = check_test_transparent_record(test, table, members, 2, TYPE_TRANSPARENT_UNION_APPLIED);
        check_test_expect(test, type_is_transparent_union(table, type_qualify(table, record, BUSTER_TYPE_CONST)) &&
                               type_is_transparent_union(table, type_aligned(table, record, 4)) &&
                               type_mark_transparent_union(table, record).status == TYPE_TRANSPARENT_UNION_APPLIED,
                          "nominal transparent metadata survives qualification/alignment and repeated validated marking");
        const TypeId function = type_function(table, BUSTER_TYPE_VOID, &record, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId first = type_function(table, BUSTER_TYPE_VOID, &ip, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId second = type_function(table, BUSTER_TYPE_VOID, &dp, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId missing = type_function(table, BUSTER_TYPE_VOID, &fp, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        check_test_transparent_relation(test, table, function, first, first);
        check_test_transparent_relation(test, table, function, second, second);
        check_test_transparent_relation(test, table, function, missing, 0);
        check_test_transparent_relation(test, table, record, ip, 0);
        check_test_transparent_relation(test, table, type_pointer(table, record), type_pointer(table, ip), 0);
        check_test_transparent_relation(test, table, type_pointer(table, function), type_pointer(table, second), type_pointer(table, second));
        check_test_expect(test, type_same_canonical(table, function, first) == TYPE_ANSWER_NO &&
                               type_same_canonical(table, record, ip) == TYPE_ANSWER_NO,
                          "transparent compatibility does not change canonical or nominal type identity");
        members[1].type = fp;
        const TypeId other = check_test_transparent_record(test, table, members, 2, TYPE_TRANSPARENT_UNION_APPLIED);
        const TypeId other_function = type_function(table, BUSTER_TYPE_VOID, &other, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        check_test_transparent_relation(test, table, function, other_function, 0);
        members[0].type = type_qualify(table, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
        members[1].type = BUSTER_TYPE_UINT;
        const TypeId qualified = check_test_transparent_record(test, table, members, 2, TYPE_TRANSPARENT_UNION_APPLIED);
        const TypeId int_type = BUSTER_TYPE_INT;
        const TypeId int_function = type_function(table, BUSTER_TYPE_VOID, &int_type, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId qualified_function = type_function(table, BUSTER_TYPE_VOID, &qualified, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        check_test_transparent_relation(test, table, qualified_function, int_function, int_function);
        members[0].type = type_atomic(table, BUSTER_TYPE_INT);
        const TypeId atomic = check_test_transparent_record(test, table, members, 2, TYPE_TRANSPARENT_UNION_APPLIED);
        const TypeId atomic_function = type_function(table, BUSTER_TYPE_VOID, &atomic, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId atomic_member_function = type_function(table, BUSTER_TYPE_VOID, &members[0].type, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        check_test_transparent_relation(test, table, atomic_function, int_function, 0);
        check_test_transparent_relation(test, table, atomic_function, atomic_member_function, atomic_member_function);
        members[0].type = type_array(table, BUSTER_TYPE_INT, 2);
        const TypeId array_record = check_test_transparent_record(test, table, members, 1, TYPE_TRANSPARENT_UNION_APPLIED);
        const TypeId array_function = type_function(table, BUSTER_TYPE_VOID, &array_record, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        check_test_transparent_relation(test, table, array_function, first, 0);
        members[0].type = BUSTER_TYPE_FLOAT;
        check_test_transparent_record(test, table, members, 1, TYPE_TRANSPARENT_UNION_FIRST_FLOAT);
        members[0].type = type_complex(table, BUSTER_TYPE_FLOAT);
        check_test_transparent_record(test, table, members, 1, TYPE_TRANSPARENT_UNION_FIRST_FLOAT);
        members[0].type = type_vector(table, BUSTER_TYPE_INT, 4, false);
        check_test_transparent_record(test, table, members, 1, TYPE_TRANSPARENT_UNION_FIRST_VECTOR);
        members[0].type = BUSTER_TYPE_INT;
        members[1].type = BUSTER_TYPE_LONG;
        check_test_transparent_record(test, table, members, 2, TYPE_TRANSPARENT_UNION_SIZE);
        members[1].type = type_aligned(table, BUSTER_TYPE_INT, 3);
        check_test_transparent_record(test, table, members, 2, TYPE_TRANSPARENT_UNION_ALIGNMENT);
        members[1].type = BUSTER_TYPE_INT;
        members[1].alignment_log2 = 3;
        check_test_transparent_record(test, table, members, 2, TYPE_TRANSPARENT_UNION_APPLIED);
        members[0].type = type_atomic(table, BUSTER_TYPE_FLOAT);
        check_test_transparent_record(test, table, members, 1, TYPE_TRANSPARENT_UNION_APPLIED);
        check_test_transparent_record(test, table, members, 0, TYPE_TRANSPARENT_UNION_EMPTY);
        const TypeId incomplete = type_tag(table, BUSTER_TYPE_KIND_UNION, 133902);
        check_test_expect(test, type_mark_transparent_union(table, incomplete).status == TYPE_TRANSPARENT_UNION_INCOMPLETE &&
                               !type_is_transparent_union(table, incomplete), "an incomplete transparent annotation has no future effect");
        check_test_expect(test, type_mark_transparent_union(table, BUSTER_TYPE_INT).status == TYPE_TRANSPARENT_UNION_NOT_UNION &&
                               !type_is_transparent_union(table, BUSTER_TYPE_INT), "ordinary types cannot acquire transparent union identity");

        Arena history = arena_reserve(65536);
        uint32_t* activations = BUSTER_ARENA_PUSH_ARRAY(&history, uint32_t, table->tags.count);
        check_test_expect(test, activations != 0, "transparent relation history allocates");
        if (activations != 0)
        {
            memset(activations, 0, table->tags.count * sizeof(*activations));
            const uint32_t tag = table->types.index[type_index(record)];
            const uint32_t positions[] = { 0, 19, 20, 21 };
            activations[tag] = 20;
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(positions); i += 1)
            {
                const bool active = positions[i] >= 20;
                check_test_expect(test, type_compatible_at(table, function, second, activations, table->tags.count, positions[i]) ==
                                       (active ? TYPE_ANSWER_YES : TYPE_ANSWER_NO) &&
                                       type_composite_at(table, function, second, activations, table->tags.count, positions[i]) == (active ? second : 0),
                                  "transparent compatibility and composite use the exact source activation cutoff");
                check_test_expect(test, type_compatible_at(table, type_pointer(table, function), type_pointer(table, second), activations,
                                                           table->tags.count, positions[i]) == (active ? TYPE_ANSWER_YES : TYPE_ANSWER_NO),
                                  "function-pointer compatibility observes transparent activation history recursively");
            }
            check_test_expect(test, type_compatible_at(table, function, second, activations, tag, UINT32_MAX) == TYPE_ANSWER_NO,
                              "missing history entries never authorize transparent matching");
            activations[tag] = UINT32_MAX;
            check_test_expect(test, type_compatible_at(table, function, second, activations, table->tags.count, UINT32_MAX) == TYPE_ANSWER_NO,
                              "the unavailable activation sentinel stays inactive at every position");
            activations[tag] = 0;
            check_test_expect(test, type_compatible_at(table, function, second, activations, table->tags.count, UINT32_MAX) == TYPE_ANSWER_NO,
                              "zero activation stays inactive at every position");
        }
        arena_release(&history);

        // A shared type DAG has 2^48 expanded leaves but only 48 different nested pairs.
        TypeId deep_left = function;
        TypeId deep_right = second;
        for (uint32_t level = 0; level < 48; level += 1)
        {
            const TypeId left_pointer = type_pointer(table, deep_left);
            const TypeId right_pointer = type_pointer(table, deep_right);
            const TypeId left_parameters[] = { left_pointer, left_pointer };
            const TypeId right_parameters[] = { right_pointer, right_pointer };
            deep_left = type_function(table, BUSTER_TYPE_VOID, left_parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE);
            deep_right = type_function(table, BUSTER_TYPE_VOID, right_parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE);
        }
        check_test_transparent_relation(test, table, deep_left, deep_right, deep_right);
        const TypeId recursive = type_tag(table, BUSTER_TYPE_KIND_UNION, 133903);
        const TypeId p = type_pointer(table, type_function(table, BUSTER_TYPE_VOID, &recursive, 1, BUSTER_TYPE_FLAG_PROTOTYPE));
        members[0] = (TypeMember){ .name = 1, .type = p, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
        check_test_expect(test, type_complete_record(table, recursive, members, 1, (TypeRecordAttributes){ 0 }) &&
                               type_mark_transparent_union(table, recursive).status == TYPE_TRANSPARENT_UNION_APPLIED,
                          "recursive callback union validates after complete layout");
        const TypeId q = type_pointer(table, type_function(table, BUSTER_TYPE_VOID, &p, 1, BUSTER_TYPE_FLAG_PROTOTYPE));
        const TypeId recursive_function = type_function(table, BUSTER_TYPE_VOID, &recursive, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId callback_function = type_function(table, BUSTER_TYPE_VOID, &q, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        check_test_transparent_relation(test, table, recursive_function, callback_function, callback_function);

        Arena graph = arena_reserve(65536);
        const uint64_t maximum = graph.capacity;
        memset(arena_push(&graph, maximum, 1), 0xa5, maximum);
        uint64_t required = 0;
        uint32_t answered = 0;
        uint32_t full = 0;
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            const uint64_t iterations = pass == 0 ? 1 : required + 1;
            for (uint64_t room = 0; room < iterations; room += 1)
            {
                graph.length = 31;
                graph.capacity = pass == 0 ? maximum : 31 + room;
                TypeTransparentSearch search = { .arena = &graph };
                const TypeAnswer answer = type_transparent_query(&search, table, function, second, false, 0);
                answered += answer == TYPE_ANSWER_YES;
                full += answer == TYPE_ANSWER_FULL;
                check_test_expect(test, answer == TYPE_ANSWER_YES || answer == TYPE_ANSWER_FULL,
                                  "every byte of relation-graph capacity returns the actual answer or explicit exhaustion");
                if (pass == 0)
                {
                    required = graph.length - 31;
                    const uint32_t pairs = search.pair_count;
                    const uint64_t length = graph.length;
                    check_test_expect(test, answer == TYPE_ANSWER_YES && type_transparent_query(&search, table, function, second, false, 0) == TYPE_ANSWER_YES &&
                                           search.pair_count == pairs && graph.length == length,
                                      "repeated relation queries reuse settled pairs without growth");
                    check_test_expect(test, type_transparent_query(&search, table, function, missing, false, 0) == TYPE_ANSWER_NO &&
                                           type_transparent_query(&search, table, function, first, false, 0) == TYPE_ANSWER_YES,
                                      "additional true and false roots preserve previously settled pair answers");
                }
            }
        }
        check_test_expect(test, answered != 0 && full != 0, "transparent relation capacity controls exercise success and exhaustion");
        graph.capacity = maximum;
        arena_release(&graph);
        Scratch first_scratch = scratch_begin(0);
        Scratch second_scratch = scratch_begin(first_scratch.arena);
        Arena* scratch_arenas[] = { first_scratch.arena, second_scratch.arena };
        for (uint32_t which = 0; which < 2; which += 1)
        {
            Arena* arena = scratch_arenas[which];
            const uint64_t length = arena->length;
            const uint64_t capacity = arena->capacity;
            for (uint64_t room = 0; room <= 16384; room += 64)
            {
                arena->capacity = length + room;
                table->last_error = TYPE_ERROR_NONE;
                const TypeId composite = type_composite(table, function, second);
                const TypeAnswer answer = type_compatible(table, function, second);
                check_test_expect(test, (composite == second || (composite == 0 && table->last_error == TYPE_ERROR_FULL)) &&
                                       (answer == TYPE_ANSWER_YES || answer == TYPE_ANSWER_FULL) && arena->length == length,
                                  "public transparent walks restore scratch on every success and resource failure");
            }
            arena->capacity = capacity;
        }
        scratch_end(second_scratch);
        scratch_end(first_scratch);
        type_test_table_close(&storage);
    }

    const CheckSourceFixture fixtures[] = {
        { .name = "transparent function first member both declaration orders", .source = "typedef union __attribute__((transparent_union)){int*i;double*d;}U;int f(U);int f(int*p){return p!=0;}int g(int*);int g(U p){return p.d!=0;}\n" },
        { .name = "transparent function second member both declaration orders", .source = "typedef union __attribute__((transparent_union)){int*i;double*d;}U;int f(U);int f(double*p){return p!=0;}int g(double*);int g(U p){return p.d!=0;}\n" },
        { .name = "transparent function pointer and ordinary nominal identity", .source = "typedef union __attribute__((transparent_union)){int*i;double*d;}U;typedef void F(U);typedef void I(int*);typedef void D(double*);_Static_assert(__builtin_types_compatible_p(F,I),\"first\");_Static_assert(__builtin_types_compatible_p(F*,D*),\"second\");_Static_assert(!__builtin_types_compatible_p(U,int*),\"nominal\");\n" },
        { .name = "transparent unrelated union parameters remain incompatible", .source = "typedef union __attribute__((transparent_union)){int*i;double*d;}U;typedef union __attribute__((transparent_union)){int*i;float*f;}V;typedef void F(U);typedef void G(V);_Static_assert(!__builtin_types_compatible_p(F,G),\"distinct\");\n" },
        { .name = "transparent top qualifier matching", .source = "typedef union __attribute__((transparent_union)){const int i;unsigned u;}U;typedef void F(U);typedef void I(int);_Static_assert(__builtin_types_compatible_p(F,I),\"top const\");\n" },
        { .name = "transparent atomic candidate remains distinct", .source = "typedef union __attribute__((transparent_union)){_Atomic(int)i;unsigned u;}U;typedef void F(U);typedef void I(int);typedef void A(_Atomic(int));_Static_assert(!__builtin_types_compatible_p(F,I),\"atomic\");_Static_assert(__builtin_types_compatible_p(F,A),\"atomic match\");\n" },
        { .name = "transparent array candidate does not decay", .source = "typedef union __attribute__((transparent_union)){int a[2];}U;typedef void F(U);typedef void I(int*);_Static_assert(!__builtin_types_compatible_p(F,I),\"no decay\");\n" },
        { .name = "transparent first floating member is ignored", .source = "typedef union __attribute__((transparent_union)){float f;int i;}U;void f(U);void g(void){f(1);}\n", .rejected = true },
        { .name = "transparent later floating member is allowed", .source = "typedef union __attribute__((transparent_union)){int i;float f;}U;void f(U);void f(float);\n" },
        { .name = "transparent first atomic floating member is allowed", .source = "typedef union __attribute__((transparent_union)){_Atomic(float)f;int i;}U;void f(U);void f(_Atomic(float));\n" },
        { .name = "transparent member declaration alignment does not change validation", .source = "typedef union __attribute__((transparent_union)){int i;int j __attribute__((aligned(8)));}U;void f(U);void f(int);\n" },
        { .name = "transparent incomplete forward annotation stays ignored", .source = "union __attribute__((transparent_union)) U;union U{int i;unsigned u;};void f(union U);void g(void){f(1);}\n", .rejected = true },
        { .name = "transparent composite first order constrains later calls", .source = "typedef union __attribute__((transparent_union)){int*i;double*d;}U;void f(U);void f(int*);void g(double*p){f(p);}\n", .rejected = true },
        { .name = "transparent composite reverse order constrains later calls", .source = "typedef union __attribute__((transparent_union)){int*i;double*d;}U;void f(int*);void f(U);void g(double*p){f(p);}\n", .rejected = true },
        { .name = "transparent function pointer before activation stays incompatible", .source = "union U{int*i;double*d;};void f(union U);\nvoid(*p)(int*)=f;\ntypedef union U T __attribute__((transparent_union));\n", .rejected = true },
        { .name = "transparent function pointer after activation is compatible", .source = "union U{int*i;double*d;};void f(union U);typedef union U T __attribute__((transparent_union));void(*p)(int*)=f;\n" },
        { .name = "transparent recursive callback relation terminates", .source = "union U;typedef void(*P)(union U);union __attribute__((transparent_union)) U{P p;};typedef void(*Q)(P);typedef void F(union U);typedef void G(Q);_Static_assert(__builtin_types_compatible_p(F,G),\"callback\");\n" },
    };
    char directory[] = "/tmp/buster-check-transparent-types-XXXXXX";
    bool written = mkdtemp(directory) != 0;
    char paths[BUSTER_ARRAY_LENGTH(fixtures)][160];
    char* pointers[BUSTER_ARRAY_LENGTH(fixtures)];
    ParseTestClang outcomes[BUSTER_ARRAY_LENGTH(fixtures)];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%u.c", directory, i);
        pointers[i] = paths[i];
        written = written && parse_test_write(paths[i], fixtures[i].source, strlen(fixtures[i].source));
    }
    const char* option = "-std=gnu17";
    const bool checked = written && parse_test_clang(clang, &option, 1, pointers, BUSTER_ARRAY_LENGTH(fixtures), outcomes);
    check_test_expect(test, checked, "independent transparent type oracle runs");
    for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        check_test_expect(test, outcomes[i].rejected == fixtures[i].rejected, fixtures[i].name);
        check_test_source(test, &fixtures[i], paths[i], &outcomes[i]);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        unlink(paths[i]);
    }
    rmdir(directory);
}

#endif
