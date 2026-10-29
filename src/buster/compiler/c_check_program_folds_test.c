// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #221's selector integration tests: what Tier 0, the selector the C driver runs, does with loads where it folds them
// into their consumers and where it mustn't, read from its records with the resolved effects (x86_instructions_effects).
// Tier 0 folds a local's home (a register or a frame slot) into the entry that uses it, and x = x op v into one
// read-modify-write; it folds no load through a pointer. So, by both paths' lowerings and Tier 0's twins:
//   - a volatile object is read as often as the source reads it: twice for g + g, once for a value used twice;
//   - a volatile increment reads it once and writes it once (one record or two);
//   - a load through a pointer stays on its side of a store through the same pointer;
//   - an atomic increment writes its object only with a locked form, in a call's helper;
// and natively against clang, locals whose homes are folded, read and written through pointers to them between their
// uses. Include after c_check_program_copy_test.c.

// A function's accesses to memory other than its frame's (rsp's, and rbp's with a frame pointer), in its records'
// order: 'r' a read, 'w' a write, 'm' both in one record; and whether it calls.
typedef struct CheckTestFolds CheckTestFolds;
struct CheckTestFolds
{
    char accesses[16];
    uint32_t unlocked_writes;
    bool call;
};

static CheckTestFolds check_test_folds_summary(const Tier0Code* code, Arena* arena)
{
    CheckTestFolds folds = { 0 };
    uint64_t* reads = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)code->count + 1);
    uint64_t* writes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)code->count + 1);
    uint32_t count = 0;
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
            const bool read = (reads[r] & BUSTER_X86_RESOURCE_MEMORY) != 0;
            const bool written = (writes[r] & BUSTER_X86_RESOURCE_MEMORY) != 0;
            if (operand && (read || written) && count + 1 < sizeof(folds.accesses))
            {
                folds.accesses[count] = read && written ? 'm' : read ? 'r' : 'w';
                count += 1;
            }
            folds.unlocked_writes += operand && written && (effects->memory & BUSTER_X86_MEMORY_EFFECT_ATOMIC) == 0;
            folds.call |= x86_effects_control(effects) == BUSTER_X86_CONTROL_CALL;
        }
    }
    return folds;
}

static void check_test_program_folds(CheckTest* test, const char* clang)
{
    // The functions in source order, the lowering's order of a unit's definitions, and what each must do.
    static const char source[] = "volatile int g;"
                                 "int a;"
                                 "int twice(void){return g+g;}"
                                 "int once(void){int v=g;return v+v;}"
                                 "void bump(void){g+=1;}"
                                 "int alias(int*p){int x=*p;*p=7;return x+*p;}"
                                 "int fetch(void){return __atomic_fetch_add(&a,1,__ATOMIC_SEQ_CST);}";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                              pp_run_source(&pp, "folds", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "folds: the source preprocesses");
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
        char description[160];
        snprintf(description, sizeof(description), "folds: the functions lower by the %s path", path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready && code.lowered.count == 5, description);
        static const char* const names[5] = { "g + g", "a value of g used twice", "g += 1", "a load, a store and a load through p", "an atomic increment" };
        for (uint32_t f = 0; ready && f < 5; f += 1)
        {
            const Tier0Code scalar = tier0_compile_scalar(&code.lowered.functions[f], &scratch);
            const Tier0Code vector = tier0_compile(&code.lowered.functions[f], &scratch);
            const bool compiled = scalar.error == TIER0_OK && vector.error == TIER0_OK && scalar.count == vector.count &&
                                  memcmp(scalar.records, vector.records, (uint64_t)scalar.count * sizeof(X86Instruction)) == 0;
            const CheckTestFolds folds = compiled ? check_test_folds_summary(&scalar, &scratch) : (CheckTestFolds){ { 0 }, 0, false };
            bool expected = false;
            switch (f)
            {
            case 0:
                expected = strcmp(folds.accesses, "rr") == 0;
                break;
            case 1:
                expected = strcmp(folds.accesses, "r") == 0;
                break;
            case 2:
                expected = strcmp(folds.accesses, "rw") == 0 || strcmp(folds.accesses, "m") == 0;
                break;
            case 3:
                expected = strcmp(folds.accesses, "rwr") == 0;
                break;
            default:
                // The builtin calls its helper (c_check_program_helpers.c), whose lock xadd is the write; the caller
                // itself writes nothing unlocked.
                expected = folds.call && folds.unlocked_writes == 0;
                break;
            }
            snprintf(description, sizeof(description), "folds (%s path): %s accesses memory as \"%s\"%s", path == 0 ? "scalar" : "vector", names[f], folds.accesses,
                     folds.unlocked_writes != 0 ? ", with an unlocked write" : "");
            check_test_expect(test, compiled && expected, description);
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
    // Folded homes against writes through pointers to them, natively against clang: x's home read into x + 1 before *q
    // stores 7 into it, x = x op v with x written through q between, and a value read before a call that changes it.
    check_test_program_source(test,
                              "static int set(int*p,int v){*p=v;return v;}"
                              "int tested(int n){int x=n;int*q=&x;int y=x+1;*q=7;int z=x*2;x=x+n;q[0]+=n;x=x-(*q>>1);"
                              "int w=x;int s=set(q,n+3);return y+z+x+w+s+(*q==x);}",
                              "folded-homes-and-interfering-stores", clang);
}
