// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's direct atomic loads and stores, by both paths' lowerings and Tier 0's twins: an aligned load or store of an
// integer or a pointer of a constant order its kind takes (check_program_direct_access), GNU's _n or C11's, and an
// implicit _Atomic object's (check_lower_atomic_direct_plan), is the typed stream's LOAD or SET with its atomic marker
// (BUSTER_TYPED_ACCESS_ATOMIC and the order), with no helper's call, and a seq_cst store the FENCE of seq_cst after it;
// Tier 0's code has the access, and an mfence after a seq_cst store alone, in its place among the function's accesses to
// memory, as x86_semantics.c reads them (check_test_fences_summary's trace). A dynamic order, an order its kind doesn't
// take, a scoped family's and a floating object's keep their helper, and the plan counts both. Natively against clang:
// every width and order, the C11 forms, a pointer's, an implicit _Atomic object's loads, stores and updates (whose loop
// keeps its compare-exchange's leaf). Include after c_check_program_fence_test.c, whose summary of a function's accesses
// this test takes.

// A function's atomic accesses: their markers (8 bits each, the first lowest) and their count.
typedef struct CheckTestAccesses CheckTestAccesses;
struct CheckTestAccesses
{
    uint32_t count;
    uint64_t markers;
};

static CheckTestAccesses check_test_accesses_summary(const Tier0Function* function)
{
    CheckTestAccesses accesses = { 0 };
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        const uint8_t kind = function->kinds[i];
        const uint8_t class = function->classes[i];
        const bool scalar = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64;
        if ((kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET) && scalar && function->values[i] != 0)
        {
            accesses.markers |= accesses.count < 8 ? (function->values[i] & 0xff) << (8 * accesses.count) : 0;
            accesses.count += 1;
        }
    }
    return accesses;
}

static void check_test_program_atomic_accesses(CheckTest* test, const char* clang)
{
    // The functions in source order, and what each must lower to.
    static const char source[] = "_Atomic int a;"
                                 "int load_relaxed(int*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}"
                                 "int load_acquire(int*p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}"
                                 "long load_seq(long*p){return __atomic_load_n(p,__ATOMIC_SEQ_CST);}"
                                 "void store_relaxed(short*p,short v){__atomic_store_n(p,v,__ATOMIC_RELAXED);}"
                                 "void store_release(char*p,char v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}"
                                 "void store_seq(int*p,int v){__atomic_store_n(p,v,__ATOMIC_SEQ_CST);}"
                                 "int c11(_Atomic int*p){__c11_atomic_store(p,1,__ATOMIC_SEQ_CST);return __c11_atomic_load(p,__ATOMIC_ACQUIRE);}"
                                 "int*pointer(int**p){return __atomic_load_n(p,__ATOMIC_CONSUME);}"
                                 "int implicit(int v){a=v;return a;}"
                                 "int dynamic(int*p,int o){return __atomic_load_n(p,o);}"
                                 "int scoped(int*p){return __scoped_atomic_load_n(p,__ATOMIC_ACQUIRE,0);}"
                                 "float floating(_Atomic float*p){return __c11_atomic_load(p,__ATOMIC_ACQUIRE);}";
    typedef struct CheckTestAccessCase CheckTestAccessCase;
    struct CheckTestAccessCase
    {
        const char* name;
        uint32_t accesses;
        uint64_t markers;
        uint32_t fences;
        uint32_t calls;
        const char* trace;
    };
    // A marker is BUSTER_TYPED_ACCESS_ATOMIC (0x10) with its order; a seq_cst store's FENCE has value 5 and its record an
    // mfence ('f') after its write ('w'); a load is a read ('r'), and a helper's call a call ('c'). The implicit object's
    // plan writes its value's buffer through its address first (its two 'w's before the store's), and the floating
    // load's helper result, its bits, is read back from its frame temporary through its address (the 'r' after the call).
    static const CheckTestAccessCase cases[] = {
        { "load_relaxed", 1, 0x10, 0, 0, "r" },
        { "load_acquire", 1, 0x12, 0, 0, "r" },
        { "load_seq", 1, 0x15, 0, 0, "r" },
        { "store_relaxed", 1, 0x10, 0, 0, "w" },
        { "store_release", 1, 0x13, 0, 0, "w" },
        { "store_seq", 1, 0x15, 1, 0, "wf" },
        { "c11", 2, 0x1215, 1, 0, "wfr" },
        { "pointer", 1, 0x11, 0, 0, "r" },
        { "implicit", 2, 0x1515, 1, 0, "wwwfr" },
        { "dynamic", 0, 0, 0, 1, "c" },
        { "scoped", 0, 0, 0, 1, "c" },
        { "floating", 0, 0, 0, 1, "cr" },
    };
    const uint32_t count = BUSTER_ARRAY_LENGTH(cases);
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                              pp_run_source(&pp, "atomic-accesses", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "atomic accesses: the source preprocesses");
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
        char description[224];
        snprintf(description, sizeof(description), "atomic accesses: the functions lower by the %s path", way);
        check_test_expect(test, ready && code.lowered.count == count, description);
        // The plan: nine builtins direct (the implicit object's accesses have no builtin node, and no leaf), three by
        // their helper (the dynamic order's, the scoped one's and the floating one's).
        const CheckProgramHelpers* helpers = &code.lowered.helpers;
        snprintf(description, sizeof(description), "atomic accesses (%s path): the plan takes %u builtins directly and %u by their helper", way,
                 ready ? helpers->direct[CHECK_PROGRAM_HELPER_ATOMIC_ACCESS] : 0, ready ? helpers->helper[CHECK_PROGRAM_HELPER_ATOMIC_ACCESS] : 0);
        check_test_expect(test, ready && helpers->direct[CHECK_PROGRAM_HELPER_ATOMIC_ACCESS] == 9 && helpers->helper[CHECK_PROGRAM_HELPER_ATOMIC_ACCESS] == 3, description);
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            const Tier0Function* function = &code.lowered.functions[f];
            const Tier0Code scalar = tier0_compile_scalar(function, &scratch);
            const Tier0Code vector = tier0_compile(function, &scratch);
            const bool compiled = scalar.error == TIER0_OK && vector.error == TIER0_OK && scalar.count == vector.count &&
                                  memcmp(scalar.records, vector.records, (uint64_t)scalar.count * sizeof(X86Instruction)) == 0 &&
                                  tier0_validate(function).error == TIER0_OK;
            const CheckTestFences fences = compiled ? check_test_fences_summary(function, &scalar, &scratch) : (CheckTestFences){ 0 };
            const CheckTestAccesses accesses = check_test_accesses_summary(function);
            const CheckTestAccessCase* expected = &cases[f];
            const bool same = accesses.count == expected->accesses && accesses.markers == expected->markers && fences.fences == expected->fences &&
                              fences.values == (expected->fences != 0 ? 5u : 0u) && fences.calls == expected->calls && strcmp(fences.trace, expected->trace) == 0;
            snprintf(description, sizeof(description), "atomic accesses (%s path): %s has %u marked accesses (%#llx), %u fences, %u calls and the trace \"%s\"", way,
                     expected->name, accesses.count, (unsigned long long)accesses.markers, fences.fences, fences.calls, fences.trace);
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
    // Natively against clang: loads and stores of every width and order, the C11 forms, a pointer's, an implicit
    // object's loads, stores and updates (its compare-exchange loop's first load direct), a volatile object's, and the
    // helpers' among them (a dynamic order, a scoped family's, a floating object's).
    check_test_program_source(test,
                              "_Atomic int ai;_Atomic long al;_Atomic char ac;_Atomic float af;static volatile short vs;int* gp;"
                              "int tested(int n){char c;short s;int i;long l;int*p;int o=n&3?__ATOMIC_ACQUIRE:__ATOMIC_SEQ_CST;"
                              "__atomic_store_n(&c,(char)n,__ATOMIC_RELAXED);__atomic_store_n(&s,(short)(n*3),__ATOMIC_RELEASE);"
                              "__atomic_store_n(&i,n*5,__ATOMIC_SEQ_CST);__atomic_store_n(&l,(long)n<<40,__ATOMIC_SEQ_CST);"
                              "__atomic_store_n(&gp,&i,__ATOMIC_RELEASE);p=__atomic_load_n(&gp,__ATOMIC_CONSUME);"
                              "long r=__atomic_load_n(&c,__ATOMIC_RELAXED)+__atomic_load_n(&s,__ATOMIC_ACQUIRE)+__atomic_load_n(p,__ATOMIC_SEQ_CST)+"
                              "(__atomic_load_n(&l,__ATOMIC_ACQUIRE)>>40)+__atomic_load_n(&i,o);"
                              "__c11_atomic_store(&ai,n,__ATOMIC_SEQ_CST);r+=__c11_atomic_load(&ai,__ATOMIC_ACQUIRE);"
                              "ai=(int)r;al=r*7;ac=(char)n;ai+=3;al-=n;ac++;r+=ai+al+ac;"
                              "__atomic_store_n(&vs,(short)n,__ATOMIC_RELEASE);r+=__atomic_load_n(&vs,__ATOMIC_ACQUIRE);"
                              "af=(float)n;r+=(long)(__c11_atomic_load(&af,__ATOMIC_ACQUIRE)*2);"
                              "r+=__scoped_atomic_load_n(&i,__ATOMIC_ACQUIRE,0);return (int)r;}",
                              "atomic-accesses-direct-and-helper-widths-orders-and-implicit", clang);
}
