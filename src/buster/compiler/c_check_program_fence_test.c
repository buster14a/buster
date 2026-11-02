// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1059's direct fences, by both paths' lowerings and Tier 0's twins: a fence of a constant order is the typed stream's
// FENCE of its order and scope (check_program_direct_fence), with no helper's call, and Tier 0's code has an mfence for
// each thread fence of seq_cst and nothing for the others, in its place among the function's accesses to memory, as the
// resolved effects (x86_instructions_effects) and the form's own (x86_effects: BUSTER_X86_MEMORY_EFFECT_FENCE) read
// it; a fence of a dynamic or out-of-range order, of an argument with an effect, or of a scoped family keeps its
// helper, and the plan counts both (CheckProgramHelpers.direct and .helper); and natively against clang, fences of every
// order and scope in statements, in expressions, in a statement expression and in a loop, with the helpers' among them.
// test-checker-driver runs the concurrent controls (c_check_fence_driver_test.c). Include after
// c_check_program_folds_test.c, whose summary of a function's accesses this test extends.

// A function's fences: its FENCE entries and their values (4 bits each, the first lowest), its CALL entries, and its
// records' accesses to memory other than its frame's and its fences, in order: 'r' a read, 'w' a write, 'm' both in one
// record, 'f' a record that fences, 'c' a call.
typedef struct CheckTestFences CheckTestFences;
struct CheckTestFences
{
    uint32_t fences;
    uint64_t values;
    uint32_t calls;
    char trace[16];
};

static CheckTestFences check_test_fences_summary(const Tier0Function* function, const Tier0Code* code, Arena* arena)
{
    CheckTestFences fences = { 0 };
    for (uint32_t i = 0; i < function->count; i += 1)
    {
        if (function->kinds[i] == BUSTER_TYPED_FENCE)
        {
            fences.values |= fences.fences < 16 ? (function->values[i] & 15) << (4 * fences.fences) : 0;
            fences.fences += 1;
        }
        fences.calls += function->kinds[i] == BUSTER_TYPED_CALL;
    }
    uint64_t* reads = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)code->count + 1);
    uint64_t* writes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)code->count + 1);
    uint32_t length = 0;
    if (reads != 0 && writes != 0)
    {
        x86_instructions_effects(code->records, code->count, reads, writes);
        for (uint32_t r = 0; r < code->count; r += 1)
        {
            const X86Instruction* record = &code->records[r];
            const X86Effects* effects = x86_effects(record->form);
            const uint32_t base = record->rm & 15;
            const bool frame = (record->mode & BUSTER_X86_MODE_BASE) != 0 && (base == X86_REGISTER_RSP || (base == X86_REGISTER_RBP && code->frame_pointer));
            const bool operand = (record->mode & BUSTER_X86_MODE_MEMORY) != 0 && !frame && (effects->memory & BUSTER_X86_MEMORY_EFFECT_ADDRESS) == 0;
            const bool read = operand && (reads[r] & BUSTER_X86_RESOURCE_MEMORY) != 0;
            const bool written = operand && (writes[r] & BUSTER_X86_RESOURCE_MEMORY) != 0;
            const bool fence = (effects->memory & BUSTER_X86_MEMORY_EFFECT_FENCE) != 0;
            const bool call = x86_effects_control(effects) == BUSTER_X86_CONTROL_CALL;
            const char mark = fence ? 'f' : call ? 'c' : read && written ? 'm' : read ? 'r' : written ? 'w' : 0;
            if (mark != 0 && length + 1 < sizeof(fences.trace))
            {
                fences.trace[length] = mark;
                length += 1;
            }
        }
    }
    return fences;
}

static void check_test_program_fences(CheckTest* test, const char* clang)
{
    // The functions in source order, and what each must lower to: its FENCE entries' values, its calls, its trace.
    static const char source[] = "int g;"
                                 "void sequential(void){__atomic_thread_fence(__ATOMIC_SEQ_CST);}"
                                 "void weaker(void){__atomic_thread_fence(__ATOMIC_ACQUIRE);__atomic_thread_fence(__ATOMIC_RELEASE);"
                                 "__atomic_thread_fence(__ATOMIC_ACQ_REL);__atomic_thread_fence(__ATOMIC_CONSUME);__atomic_thread_fence(__ATOMIC_RELAXED);}"
                                 "void signals(void){__atomic_signal_fence(__ATOMIC_SEQ_CST);__c11_atomic_signal_fence(__ATOMIC_ACQUIRE);}"
                                 "void legacy(void){__sync_synchronize();__c11_atomic_thread_fence(__ATOMIC_SEQ_CST);}"
                                 "int between(int n){g=n;__atomic_thread_fence(__ATOMIC_SEQ_CST);return g+n;}"
                                 "int dynamic(int o){__atomic_thread_fence(o);return o;}"
                                 "void invalid(void){__atomic_thread_fence(7);}"
                                 "void effect(void){__c11_atomic_thread_fence((g++,__ATOMIC_SEQ_CST));}"
                                 "void scoped(void){__scoped_atomic_thread_fence(__ATOMIC_SEQ_CST,0);}"
                                 "int tracked(int n){return n+({__atomic_thread_fence(__ATOMIC_SEQ_CST);__atomic_signal_fence(__ATOMIC_ACQUIRE);n*2;});}";
    typedef struct CheckTestFenceCase CheckTestFenceCase;
    struct CheckTestFenceCase
    {
        const char* name;
        uint32_t fences;
        uint64_t values;
        uint32_t calls;
        const char* trace;
    };
    // A signal fence's value has BUSTER_TYPED_FENCE_SIGNAL (8). A helper's call ends the trace of each function that
    // keeps one; `effect`'s increment is its argument's, before the call, which no trace here pins. `tracked`'s
    // statement expression makes its lowering track its stack (CheckLowerWriter.track), with fences over a live value.
    static const CheckTestFenceCase cases[] = {
        { "sequential", 1, 0x5, 0, "f" },
        { "weaker", 5, 0x01432, 0, "" },
        { "signals", 2, 0xad, 0, "" },
        { "legacy", 2, 0x55, 0, "ff" },
        { "between", 1, 0x5, 0, "wfr" },
        { "dynamic", 0, 0, 1, "c" },
        { "invalid", 0, 0, 1, "c" },
        { "effect", 0, 0, 1, NULL },
        { "scoped", 0, 0, 1, "c" },
        { "tracked", 2, 0xa5, 0, "f" },
    };
    const uint32_t count = BUSTER_ARRAY_LENGTH(cases);
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                              pp_run_source(&pp, "fences", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "fences: the source preprocesses");
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
        char description[192];
        snprintf(description, sizeof(description), "fences: the functions lower by the %s path", way);
        check_test_expect(test, ready && code.lowered.count == count, description);
        // The plan: thirteen fences direct, four with their helper, one signature for them all (the thread fence's).
        const CheckProgramHelpers* helpers = &code.lowered.helpers;
        uint32_t planned = 0;
        for (uint32_t h = 0; ready && h < helpers->count; h += 1)
        {
            planned += check_program_helper_family(&helpers->items[h]) == CHECK_PROGRAM_HELPER_FENCE;
        }
        snprintf(description, sizeof(description), "fences (%s path): the plan takes %u fences directly and %u by their helper, of %u signatures", way,
                 ready ? helpers->direct[CHECK_PROGRAM_HELPER_FENCE] : 0, ready ? helpers->helper[CHECK_PROGRAM_HELPER_FENCE] : 0, planned);
        check_test_expect(test, ready && helpers->direct[CHECK_PROGRAM_HELPER_FENCE] == 13 && helpers->helper[CHECK_PROGRAM_HELPER_FENCE] == 4 && planned == 1,
                          description);
        for (uint32_t f = 0; ready && f < count; f += 1)
        {
            const Tier0Function* function = &code.lowered.functions[f];
            const Tier0Code scalar = tier0_compile_scalar(function, &scratch);
            const Tier0Code vector = tier0_compile(function, &scratch);
            const bool compiled = scalar.error == TIER0_OK && vector.error == TIER0_OK && scalar.count == vector.count &&
                                  memcmp(scalar.records, vector.records, (uint64_t)scalar.count * sizeof(X86Instruction)) == 0 &&
                                  tier0_validate(function).error == TIER0_OK;
            const CheckTestFences fences = compiled ? check_test_fences_summary(function, &scalar, &scratch) : (CheckTestFences){ 0 };
            const CheckTestFenceCase* expected = &cases[f];
            const bool same = fences.fences == expected->fences && fences.values == expected->values && fences.calls == expected->calls &&
                              (expected->trace == NULL || strcmp(fences.trace, expected->trace) == 0);
            snprintf(description, sizeof(description), "fences (%s path): %s has %u FENCE entries (values %#llx), %u calls and the trace \"%s\"", way,
                     expected->name, fences.fences, (unsigned long long)fences.values, fences.calls, fences.trace);
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
    // Natively against clang: every order and scope, a fence in an expression, in a statement expression and in a loop,
    // and the helpers' fences (a dynamic order, an order past seq_cst and an argument whose increment runs once) beside
    // the direct ones.
    check_test_program_source(test,
                              "int g;static int k;int order(int o){__atomic_thread_fence(o);return o;}"
                              "int tested(int n){int a=0;g=n;__atomic_thread_fence(__ATOMIC_SEQ_CST);a+=g;__atomic_thread_fence(__ATOMIC_ACQUIRE);"
                              "__atomic_signal_fence(__ATOMIC_RELEASE);a+=(__atomic_thread_fence(__ATOMIC_ACQ_REL),g*2);__sync_synchronize();"
                              "__c11_atomic_thread_fence(__ATOMIC_SEQ_CST);__c11_atomic_signal_fence(__ATOMIC_RELAXED);"
                              "__c11_atomic_thread_fence((k++,__ATOMIC_SEQ_CST));__atomic_thread_fence(7);__atomic_thread_fence(__ATOMIC_CONSUME);"
                              "a+=order(n&3)+k;a+=n*({__atomic_thread_fence(__ATOMIC_SEQ_CST);__atomic_signal_fence(__ATOMIC_ACQUIRE);g+1;});for(int i=0;i<n%5;i++){__atomic_thread_fence(__ATOMIC_SEQ_CST);a+=i;__atomic_signal_fence(__ATOMIC_SEQ_CST);}return a;}",
                              "fences-direct-and-helper-orders-and-scopes", clang);
}
