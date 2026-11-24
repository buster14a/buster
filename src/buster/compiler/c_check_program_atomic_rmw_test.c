// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's direct atomic read/modify/writes, by both paths' lowerings and Tier 0's twins: an exchange (a test-and-set's
// of its byte too), or an addition or a subtraction returning the old value or the new one, GNU's, __sync's or C11's,
// unscoped, of an aligned integer, enumeration or pointer (an exchange's) of a constant order (check_program_direct_rmw),
// and an implicit _Atomic integer's increment, decrement, += or -= (check_program_atomic_update_direct), is the typed
// stream's ATOMIC_RMW (its value BUSTER_TYPED_RMW_ADD for an addition, and the order), with no helper's call; Tier 0's
// code has its one locked access to memory, xchg or lock xadd, as x86_semantics.c reads it (a read and a write:
// check_test_fences_summary's 'm'). A compare-exchange, GNU's, __sync's (bool and val) or C11's, of a constant order
// (check_program_direct_cas), and every plan's (a generic one's, and an implicit update's loop other than an addition),
// is the typed stream's ATOMIC_CAS (its value's low bits the order), lock cmpxchg; GNU's and C11's read the expected
// value where it points ('r') and write the old one there where it isn't the expected one ('w'). A bitwise, minimum or
// maximum one, GNU's, __sync's or C11's, of a constant order (check_program_direct_fetch), is a loop of ATOMIC_CAS after a
// relaxed atomic load ('r'). A dynamic order keeps its helper, the plan's use of it. Natively against clang: every width
// and operation, the __sync and C11 forms, a pointer's and a test-and-set's, compare-exchanges of every width that
// succeed and fail, minimums and maximums on both sides of each width's sign, and an implicit object's updates. Include after c_check_program_atomic_access_test.c, whose summary of a function's
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

// A function's compare-exchanges: their orders (8 bits each, the first lowest) and their count.
static CheckTestAccesses check_test_cas_summary(const Tier0Function* function)
{
    CheckTestAccesses cas = { 0 };
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        if (function->kinds[i] == BUSTER_TYPED_ATOMIC_CAS)
        {
            cas.markers |= cas.count < 8 ? (function->values[i] & BUSTER_TYPED_FENCE_ORDER) << (8 * cas.count) : 0;
            cas.count += 1;
        }
    }
    return cas;
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
                                 "_Bool sync_bool(char*p,char e){return __sync_bool_compare_and_swap(p,e,1);}"
                                 "long sync_val(long*p){return __sync_val_compare_and_swap(p,3,4);}"
                                 "_Bool c11_compare(_Atomic short*p,short*e){return __c11_atomic_compare_exchange_weak(p,e,7,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}"
                                 "_Bool generic(int*p,int*e,int*d){return __atomic_compare_exchange(p,e,d,1,__ATOMIC_RELEASE,__ATOMIC_RELAXED);}"
                                 "int bitwise(int*p){return __atomic_fetch_or(p,1,__ATOMIC_RELAXED);}"
                                 "unsigned char nand(unsigned char*p){return __atomic_nand_fetch(p,0x0f,__ATOMIC_ACQ_REL);}"
                                 "long legacy_xor(long*p,long v){return __sync_xor_and_fetch(p,v);}"
                                 "signed char minimum(signed char*p,signed char v){return __atomic_fetch_min(p,v,__ATOMIC_SEQ_CST);}"
                                 "unsigned umax(unsigned*p,unsigned v){return __sync_fetch_and_umax(p,v);}"
                                 "int c11_and(_Atomic int*p){return __c11_atomic_fetch_and(p,6,__ATOMIC_RELEASE);}"
                                 "int dynamic(int*p,int o){return __atomic_fetch_add(p,1,o);}"
                                 "_Bool compare_dynamic(int*p,int*e,int o){return __atomic_compare_exchange_n(p,e,1,0,o,__ATOMIC_RELAXED);}"
                                 "int multiplied(void){return a*=3;}";
    typedef struct CheckTestRmwCase CheckTestRmwCase;
    struct CheckTestRmwCase
    {
        const char* name;
        uint32_t rmws;
        uint64_t values;
        uint32_t cas;
        uint64_t orders;
        uint32_t calls;
        const char* trace;
    };
    // A value is BUSTER_TYPED_RMW_ADD (0x08) for an addition or a subtraction, with its order (a __sync builtin's
    // seq_cst); a compare-exchange's order its own (a __sync builtin's and a plan's seq_cst), a bitwise, minimum or
    // maximum loop's its builtin's; its code's locked access a read and a write ('m'), a helper's call a call ('c'). The
    // implicit object's increment takes its address from no frame temporary; a generic compare-exchange's buffers' copies
    // and its compound multiplication's loop (its first load direct) reach their accesses through registers too, so that
    // their traces are read for the locked access and no call alone.
    static const CheckTestRmwCase cases[] = {
        { "exchange", 1, 0x00, 0, 0, 0, "m" },
        { "fetch_add", 1, 0x0a, 0, 0, 0, "m" },
        { "sub_fetch", 1, 0x0d, 0, 0, 0, "m" },
        { "legacy", 1, 0x0d, 0, 0, 0, "m" },
        { "test_and_set", 1, 0x04, 0, 0, 0, "m" },
        { "pointer", 1, 0x03, 0, 0, 0, "m" },
        { "c11", 1, 0x09, 0, 0, 0, "m" },
        { "implicit", 1, 0x0d, 0, 0, 0, "m" },
        { "compare", 0, 0, 1, 0x05, 0, "rmw" },
        { "sync_bool", 0, 0, 1, 0x05, 0, "m" },
        { "sync_val", 0, 0, 1, 0x05, 0, "m" },
        { "c11_compare", 0, 0, 1, 0x02, 0, "rmw" },
        { "generic", 0, 0, 1, 0x05, 0, NULL },
        { "bitwise", 0, 0, 1, 0x00, 0, "rm" },
        { "nand", 0, 0, 1, 0x04, 0, "rm" },
        { "legacy_xor", 0, 0, 1, 0x05, 0, "rm" },
        { "minimum", 0, 0, 1, 0x05, 0, "rm" },
        { "umax", 0, 0, 1, 0x05, 0, "rm" },
        { "c11_and", 0, 0, 1, 0x03, 0, "rm" },
        { "dynamic", 0, 0, 0, 0, 1, "c" },
        { "compare_dynamic", 0, 0, 0, 0, 1, "c" },
        { "multiplied", 0, 0, 1, 0x05, 0, NULL },
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
        const CheckOptions checking = { 0 };
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
        // The functions' access column in the output (check_program_access), as benchmark-backend-source compiles them
        // again: the emission's own is its scratch's, which the compilations below reuse.
        uint8_t* defined = 0;
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &input_arena, &scratch) &&
                check_program_emit(&code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, path == 0, &output, &bytes, &scratch, NULL, NULL, false, true) &&
                check_program_access(&code, &program, &output, &scratch, &defined);
        const char* const way = path == 0 ? "scalar" : "vector";
        char description[256];
        snprintf(description, sizeof(description), "atomic read/modify/writes: the functions lower by the %s path", way);
        check_test_expect(test, ready && code.lowered.count == count, description);
        // The lowered code: eight read/modify/writes, seven builtins' and the implicit object's increment (which has no
        // builtin node), and twelve compare-exchanges, five builtins', six bitwise, minimum and maximum loops' and the
        // multiplication's loop's; the plan: two builtins by their helper (the dynamic orders'), and no leaf of any other,
        // which none uses.
        const CheckProgramHelpers* helpers = &code.lowered.helpers;
        uint32_t uses = 0;
        uint32_t direct = 0;
        uint32_t exchanges = 0;
        for (uint32_t u = 0; ready && u < helpers->use_count; u += 1)
        {
            uses += check_program_helper_family(&helpers->items[helpers->uses[u].helper]) == CHECK_PROGRAM_HELPER_ATOMIC_RMW;
        }
        for (uint32_t f = 0; ready && f < code.lowered.count; f += 1)
        {
            direct += check_test_rmws_summary(&code.lowered.functions[f]).count;
            exchanges += check_test_cas_summary(&code.lowered.functions[f]).count;
        }
        snprintf(description, sizeof(description),
                 "atomic read/modify/writes (%s path): the code takes %u directly and %u compare-exchanges, and the plan %u builtins by their helper of %u helpers",
                 way, direct, exchanges, uses, helpers->count);
        check_test_expect(test, ready && direct == 8 && exchanges == 12 && uses == 2 && helpers->count == 2, description);
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            const Tier0Function* function = &code.lowered.functions[f];
            const Tier0Code vector = tier0_compile(function, &scratch);
            const bool compiled = vector.error == TIER0_OK && x86_validate(vector.records, vector.count) == vector.count &&
                                  tier0_validate(function).error == TIER0_OK;
            const CheckTestFences fences = compiled ? check_test_fences_summary(function, &vector, &scratch) : (CheckTestFences){ 0 };
            const CheckTestAccesses rmws = check_test_rmws_summary(function);
            const CheckTestAccesses cas = check_test_cas_summary(function);
            const CheckTestRmwCase* expected = &cases[f];
            // The generic compare-exchange's and the compound multiplication's: one locked access among others, no call.
            const bool traced = expected->trace != NULL ? strcmp(fences.trace, expected->trace) == 0 : strchr(fences.trace, 'c') == NULL && strchr(fences.trace, 'm') != NULL &&
                                                                                                      strchr(fences.trace, 'm') == strrchr(fences.trace, 'm');
            const bool same = rmws.count == expected->rmws && rmws.markers == expected->values && cas.count == expected->cas && cas.markers == expected->orders &&
                              fences.fences == 0 && fences.calls == expected->calls && traced;
            snprintf(description, sizeof(description),
                     "atomic read/modify/writes (%s path): %s has %u read/modify/writes (%#llx), %u compare-exchanges (%#llx), %u calls and the trace \"%s\"", way,
                     expected->name, rmws.count, (unsigned long long)rmws.markers, cas.count, (unsigned long long)cas.markers, fences.calls, fences.trace);
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
    // width (its multiplication's loop, and a byte's and a bool's, compare-exchanges'), compare-exchanges of every width
    // and form, GNU's, __sync's, C11's and the generic one, that succeed and that fail (their results, the expected
    // objects and the objects), bitwise, minimum and maximum loops of every width, signed and unsigned on both sides of
    // the sign (a bool's bitwise ones too), and the helpers' among them (dynamic orders). Defined for every input the
    // harness gives (n from -3 to 19, which UBSan's build of clang's code runs clean): an unsigned checksum, and wide
    // signed values multiplied, not shifted.
    check_test_program_suite(test, "checker/programs/program-atomic-rmws", clang);
}
