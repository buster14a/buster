// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's direct atomic read/modify/writes, by both paths' lowerings and Tier 0's twins: an exchange (a test-and-set's
// of its byte too), or an addition or a subtraction returning the old value or the new one, GNU's, __sync's or C11's,
// unscoped, of an aligned integer, enumeration or pointer (an exchange's) of a constant order (check_program_direct_rmw),
// and an implicit _Atomic integer's increment, decrement, += or -= (check_program_atomic_update_direct), is the typed
// stream's ATOMIC_RMW (its value BUSTER_TYPED_RMW_ADD for an addition, and the order), with no helper's call; Tier 0's
// code has its one locked access to memory, xchg or lock xadd, as x86_semantics.c reads it (a read and a write:
// check_test_fences_summary's 'm'). A compare-exchange, a bitwise operation, a dynamic order and an implicit update
// other than an addition keep their helper (the last, a compare-exchange's loop), the plan's use of it. Natively
// against clang: every width and operation, the __sync and C11 forms, a pointer's and a test-and-set's, and an
// implicit object's updates. Include after c_check_program_atomic_access_test.c, whose summary of a function's
// accesses this test reads alongside its own.

// A function's read/modify/writes: their values (8 bits each, the first lowest) and their count.
static CheckTestAccesses check_test_rmws_summary(const Tier0Function* function)
{
    CheckTestAccesses rmws = { 0 };
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        if (function->kinds[i] == BUSTER_TYPED_ATOMIC_RMW)
        {
            rmws.markers |= rmws.count < 8 ? (function->values[i] & 0xff) << (8 * rmws.count) : 0;
            rmws.count += 1;
        }
    }
    return rmws;
}

static void check_test_program_atomic_rmws(CheckTest* test, const char* clang)
{
    // The functions in source order, and what each must lower to.
    static const char source[] = "_Atomic int a;"
                                 "int exchange(int*p,int v){return __atomic_exchange_n(p,v,__ATOMIC_RELAXED);}"
                                 "long fetch_add(long*p){return __atomic_fetch_add(p,3,__ATOMIC_ACQUIRE);}"
                                 "short sub_fetch(short*p,short v){return __atomic_sub_fetch(p,v,__ATOMIC_SEQ_CST);}"
                                 "int legacy(int*p){return __sync_fetch_and_add(p,1);}"
                                 "_Bool test_and_set(char*p){return __atomic_test_and_set(p,__ATOMIC_ACQ_REL);}"
                                 "int*pointer(int**p,int*v){return __atomic_exchange_n(p,v,__ATOMIC_RELEASE);}"
                                 "int c11(_Atomic int*p){return __c11_atomic_fetch_sub(p,2,__ATOMIC_CONSUME);}"
                                 "int implicit(void){return ++a;}"
                                 "_Bool compare(int*p,int*e){return __atomic_compare_exchange_n(p,e,1,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);}"
                                 "int bitwise(int*p){return __atomic_fetch_or(p,1,__ATOMIC_RELAXED);}"
                                 "int dynamic(int*p,int o){return __atomic_fetch_add(p,1,o);}"
                                 "int multiplied(void){return a*=3;}";
    typedef struct CheckTestRmwCase CheckTestRmwCase;
    struct CheckTestRmwCase
    {
        const char* name;
        uint32_t rmws;
        uint64_t values;
        uint32_t calls;
        const char* trace;
    };
    // A value is BUSTER_TYPED_RMW_ADD (0x08) for an addition or a subtraction, with its order (a __sync builtin's
    // seq_cst); its code's locked access a read and a write ('m'), a helper's call a call ('c'). The implicit object's
    // increment takes its address from no frame temporary; its compound multiplication's loop loads the object directly
    // ('r') and calls the compare-exchange's leaf.
    static const CheckTestRmwCase cases[] = {
        { "exchange", 1, 0x00, 0, "m" },
        { "fetch_add", 1, 0x0a, 0, "m" },
        { "sub_fetch", 1, 0x0d, 0, "m" },
        { "legacy", 1, 0x0d, 0, "m" },
        { "test_and_set", 1, 0x04, 0, "m" },
        { "pointer", 1, 0x03, 0, "m" },
        { "c11", 1, 0x09, 0, "m" },
        { "implicit", 1, 0x0d, 0, "m" },
        { "compare", 0, 0, 1, "c" },
        { "bitwise", 0, 0, 1, "c" },
        { "dynamic", 0, 0, 1, "c" },
        { "multiplied", 0, 0, 1, NULL },
    };
    const uint32_t count = BUSTER_ARRAY_LENGTH(cases);
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                              pp_run_source(&pp, "atomic-rmws", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "atomic read/modify/writes: the source preprocesses");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena bytes = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        CheckProgramCode code = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && bytes.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, preprocessing.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0 };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &input_arena, &scratch) &&
                check_program_emit(&code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0, &output, &bytes, &scratch, NULL, NULL, false, true);
        const char* const way = path == 0 ? "scalar" : "vector";
        char description[256];
        snprintf(description, sizeof(description), "atomic read/modify/writes: the functions lower by the %s path", way);
        check_test_expect(test, ready && code.lowered.count == count, description);
        // The lowered code: eight read/modify/writes, seven builtins' and the implicit object's increment (which has no
        // builtin node); the plan: three builtins by their helper (the compare-exchange's, the bitwise one's and the dynamic
        // order's).
        const CheckProgramHelpers* helpers = &code.lowered.helpers;
        uint32_t uses = 0;
        uint32_t direct = 0;
        for (uint32_t u = 0; ready && u < helpers->use_count; u += 1)
        {
            uses += check_program_helper_family(&helpers->items[helpers->uses[u].helper]) == CHECK_PROGRAM_HELPER_ATOMIC_RMW;
        }
        for (uint32_t f = 0; ready && f < code.lowered.count; f += 1)
        {
            direct += check_test_rmws_summary(&code.lowered.functions[f]).count;
        }
        snprintf(description, sizeof(description), "atomic read/modify/writes (%s path): the code takes %u directly and the plan %u builtins by their helper", way,
                 direct, uses);
        check_test_expect(test, ready && direct == 8 && uses == 3, description);
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            const Tier0Function* function = &code.lowered.functions[f];
            const Tier0Code scalar = tier0_compile_scalar(function, &scratch);
            const Tier0Code vector = tier0_compile(function, &scratch);
            const bool compiled = scalar.error == TIER0_OK && vector.error == TIER0_OK && scalar.count == vector.count &&
                                  memcmp(scalar.records, vector.records, (uint64_t)scalar.count * sizeof(X86Instruction)) == 0 &&
                                  tier0_validate(function).error == TIER0_OK;
            const CheckTestFences fences = compiled ? check_test_fences_summary(function, &scalar, &scratch) : (CheckTestFences){ 0 };
            const CheckTestAccesses rmws = check_test_rmws_summary(function);
            const CheckTestRmwCase* expected = &cases[f];
            // The compound multiplication's loop: its first load direct, and the leaf's call, among others' accesses.
            const bool traced = expected->trace != NULL ? strcmp(fences.trace, expected->trace) == 0 : strchr(fences.trace, 'c') != NULL && strchr(fences.trace, 'm') == NULL;
            const bool same = rmws.count == expected->rmws && rmws.markers == expected->values && fences.fences == 0 && fences.calls == expected->calls && traced;
            snprintf(description, sizeof(description), "atomic read/modify/writes (%s path): %s has %u read/modify/writes (%#llx), %u calls and the trace \"%s\"", way,
                     expected->name, rmws.count, (unsigned long long)rmws.markers, fences.calls, fences.trace);
            check_test_expect(test, compiled && same, description);
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    // Natively against clang: exchanges and additions of every width and order, returning the old value and the new
    // one, the __sync and C11 forms, a pointer's exchange and a test-and-set, an implicit object's updates of each
    // width (its multiplication's loop a helper's), and the helpers' among them (a compare-exchange, a bitwise operation,
    // a dynamic order).
    check_test_program_source(test,
                              "_Atomic int ai;_Atomic long al;_Atomic char ac;_Atomic short as;int* gp;"
                              "int tested(int n){char c=1;short s=2;int i=3;long l=4;unsigned char t=0;int o=n&3?__ATOMIC_ACQUIRE:__ATOMIC_SEQ_CST;"
                              "long r=__atomic_exchange_n(&c,(char)n,__ATOMIC_RELAXED)+__atomic_fetch_add(&s,(short)(n*3),__ATOMIC_RELEASE);"
                              "r+=__atomic_add_fetch(&i,n*5,__ATOMIC_SEQ_CST)+__atomic_sub_fetch(&l,(long)n<<40,__ATOMIC_ACQ_REL)+__atomic_fetch_sub(&c,(char)7,__ATOMIC_CONSUME);"
                              "r+=__sync_fetch_and_add(&i,2)+__sync_sub_and_fetch(&s,(short)9)+__sync_lock_test_and_set(&l,(long)n)+__sync_swap(&i,n+1);"
                              "r+=__atomic_test_and_set(&t,__ATOMIC_ACQUIRE)+__atomic_test_and_set(&t,__ATOMIC_SEQ_CST)+t;"
                              "gp=&i;r+=*__atomic_exchange_n(&gp,&i,__ATOMIC_SEQ_CST)+(gp==&i);"
                              "ai=n;al=n;ac=(char)n;as=(short)n;"
                              "r+=__c11_atomic_fetch_add(&ai,4,__ATOMIC_RELAXED)+__c11_atomic_exchange(&al,(long)n*11,__ATOMIC_ACQUIRE)+__c11_atomic_fetch_sub(&as,(short)5,__ATOMIC_SEQ_CST);"
                              "r+=ai++ + --al + (ac+=3) + (as-=n) + (ai*=3);"
                              "int e=i;r+=__atomic_compare_exchange_n(&i,&e,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)+__atomic_fetch_or(&i,8,__ATOMIC_RELAXED)+__atomic_fetch_add(&i,1,o);"
                              "return (int)(r+i+l+c+s+ai+al+ac+as);}",
                              "atomic-rmws-direct-and-helper-widths-orders-and-implicit", clang);
}
