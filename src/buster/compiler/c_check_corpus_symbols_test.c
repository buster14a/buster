// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-checker-corpus --symbols' reasons (#129, and #418's CI) on an original fixture, through check_corpus_measure as
// the command runs it, against clang -O0 -c: the callees of a constant if's dead arms (dropped), a const scalar that
// clang reads as a constant (folded), a dead ?: arm in a branch's condition (unselected) and a local always_inline
// function that clang inlines at its call (inlined) must each be explained. Asm-labelled objects use their emitted
// names. Three prototype-only function labels are deliberately unexplained: the current matcher reads the definition's
// attributes, so name-first pruning must preserve those differences too. With clang alone given `-Dwide=renamed`,
// the changed emitted name adds two unexplained differences; renaming C identifiers with stable asm labels adds none.
// Include after c_check_corpus.c; test-checker calls this through its forward declaration.
#ifndef BUSTER_C_CHECK_CORPUS_SYMBOLS_TEST_INCLUDED
#define BUSTER_C_CHECK_CORPUS_SYMBOLS_TEST_INCLUDED

// check_corpus_symbol_dropped_data of `name` through an index of the selections as they are now, built in `arena` and
// released after it.
static bool check_test_corpus_symbol_dropped(const CheckInput* input, const CheckProgramSelection* selected, const CheckProgramCode* code,
                                             const CheckProgramSelection* kept, const LoweringNames* names, const LoweringSymbol* name,
                                             uint8_t* explained, Arena* arena)
{
    const uint64_t mark = arena->length;
    const CheckProgramSelection reached = { 0 };
    CheckCorpusSymbolIndex index = { 0 };
    const bool dropped = check_corpus_symbol_index(&index, input, NULL, NULL, selected, code, &reached, kept, names, arena) &&
                         check_corpus_symbol_dropped_data(&index, name, explained);
    arena_pop_to(arena, mark);
    return dropped;
}

static void check_test_corpus_symbol_object_budget(CheckTest* test)
{
    Arena arena = arena_reserve(UINT64_C(1) << 16);
    CheckProgramCode code = { 0 };
    bool ready = arena.base != NULL && link_initialize(&code.link, &arena, &arena, (LinkCapacity){ .names = 3, .atoms = 3, .symbols = 3 });
    SymbolId object_symbols[4] = { 0 };
    uint32_t declarations[4] = { BUSTER_CHECK_NONE, 1, 2, 3 };
    uint8_t kinds[4] = { 0, C_BINDING_OBJECT, C_BINDING_OBJECT, C_BINDING_OBJECT };
    uint8_t values[3][4] = { { 1 }, { 2 }, { 3 } };
    for (uint32_t i = 0; ready && i < 3; i += 1)
    {
        char spelling[32];
        const int length = snprintf(spelling, sizeof(spelling), "owner.slot.%u", i + 1);
        const NameId name = link_add_name(&code.link, (const uint8_t*)spelling, (uint32_t)length);
        const AtomId atom = link_add_atom(&code.link, BUSTER_LINK_CLASS_DATA, 2, 4, values[i], 0);
        object_symbols[i + 1] = link_add_symbol(&code.link, name, atom, 0, 4, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_LOCAL, BUSTER_LINK_SOURCE_PROGRAM);
        ready = name != 0 && atom != 0 && object_symbols[i + 1] == i + 1;
    }
    check_test_expect(test, ready && link_check(&code.link) == LINK_ERROR_NONE, "--symbols collision control has defined, distinct emitted source objects");
    if (ready)
    {
        const CheckInput input = { .token_count = 4, .declaration_kinds = kinds };
        CheckDataObject objects[3] = { { .object = 1 }, { .object = 2 }, { .object = 3 } };
        const CheckProgramSelection selected = {
            .program = { .object_count = 4, .object_symbols = object_symbols, .object_declarations = declarations },
            .data = { .objects = objects, .count = 3 },
        };
        CheckProgramSelection kept = { .data = { .objects = objects + 1, .count = 2 } };
        String8 base = BUSTER_S8("owner.slot");
        LoweringNames names = { .bases = &base, .base_count = 1 };
        LoweringSymbol difference = { .name = code.link.names.bytes[1], .length = 10, .spelled = 12,
                                      .type = BUSTER_LINK_SYMBOL_OBJECT, .binding = BUSTER_LINK_SYMBOL_LOCAL, .uniqued = true };
        uint8_t explained[4] = { 0 };
        LoweringSymbol wrong = difference;
        wrong.type = BUSTER_LINK_SYMBOL_FUNCTION;
        check_test_expect(test, !check_test_corpus_symbol_dropped(&input, &selected, &code, &kept, &names, &wrong, explained, &arena),
                          "--symbols does not explain an emitted type mismatch as dropped data");
        wrong = difference;
        wrong.binding = BUSTER_LINK_SYMBOL_GLOBAL;
        check_test_expect(test, !check_test_corpus_symbol_dropped(&input, &selected, &code, &kept, &names, &wrong, explained, &arena),
                          "--symbols does not explain an emitted binding mismatch as dropped data");
        wrong = difference;
        wrong.uniqued = false;
        check_test_expect(test, !check_test_corpus_symbol_dropped(&input, &selected, &code, &kept, &names, &wrong, explained, &arena),
                          "--symbols keeps exact user names distinct from normalized shadowed names");
        String8 label = { .pointer = (char*)code.link.names.bytes[1], .length = code.link.names.length[1] };
        names.labels = &label;
        names.label_count = 1;
        check_test_expect(test, !check_test_corpus_symbol_dropped(&input, &selected, &code, &kept, &names, &difference, explained, &arena),
                          "--symbols does not normalize away a user asm label's numeric suffix");
        names.labels = NULL;
        names.label_count = 0;
        const AtomId atom = code.link.symbols.atom[1];
        code.link.symbols.atom[1] = 0;
        check_test_expect(test, !check_test_corpus_symbol_dropped(&input, &selected, &code, &kept, &names, &difference, explained, &arena),
                          "--symbols requires a defined emitted object");
        code.link.symbols.atom[1] = atom;
        kept.data.objects = objects;
        kept.data.count = 3;
        check_test_expect(test, !check_test_corpus_symbol_dropped(&input, &selected, &code, &kept, &names, &difference, explained, &arena),
                          "--symbols does not drop an exact source object retained by the model");
        kept.data.objects = objects + 1;
        kept.data.count = 2;
        uint32_t justified = 0;
        for (uint32_t i = 0; i < 3; i += 1)
        {
            difference.name = code.link.names.bytes[i + 1];
            justified += check_test_corpus_symbol_dropped(&input, &selected, &code, &kept, &names, &difference, explained, &arena);
        }
        check_test_expect(test, justified == 1 && explained[1] == 1 && explained[2] == 0 && explained[3] == 0,
                          "--symbols cannot reuse one dropped object's proof for multiple equal-key differences");
    }
    arena_release(&arena);
}

static __attribute__((noinline)) void check_test_corpus_symbol_reasons(CheckTest* test, const char* clang)
{
    check_test_corpus_symbol_object_budget(test);
    static const char source[] =
        "static inline int narrow(int n){return n+1;}static inline int wide(int n){return n+2;}static inline int widest(int n){return n+3;}\n"
        "int dropped(int n){long x=n;int r;if(sizeof(x)==sizeof(int))r=narrow(n);else if(sizeof(x)==sizeof(long))r=wide(n);else r=widest(n);return r;}\n"
        "static const int limit=5;int folded(int n){return n<limit;}\n"
        "static int unselected_call(void){return 1;}int unselected(int n){if(sizeof(n)==4?n:unselected_call())return 1;return 0;}\n"
        "static inline __attribute__((always_inline)) int inlined_call(int n){return n*3;}int inlined(int n){return inlined_call(n)+1;}\n"
        // Match the emitted asm labels, including after Clang alone renames the C identifiers. Nearby names stay distinct.
        "static int asm_dropped(int n)__asm__(\"reason.alias.dropped\");static int asm_dropped(int n){return n+8;}"
        "int asm_dead_if(int n){if(0)return asm_dropped(n);return n;}\n"
        "int asm_decoy(int n)__asm__(\"reason.alias.dropped.extra\");int asm_decoy(int n){return n;}\n"
        "static int asm_unselected(void)__asm__(\"reason.alias.unselected\");static int asm_unselected(void){return 2;}"
        "int asm_dead_conditional(int n){if(sizeof(n)==4?n:asm_unselected())return 1;return 0;}\n"
        "static int asm_dropped_data __asm__(\"reason.alias.dropped.data\")=7;"
        "int asm_dead_data(int n){if(0)return asm_dropped_data;return n;}\n"
        "static int asm_unselected_data __asm__(\"reason.alias.unselected.data\")=8;"
        "int asm_conditional_data(int n){if(sizeof(n)==4?n:asm_unselected_data)return 1;return 0;}\n"
        "static const int asm_limit __asm__(\"reason.alias.folded\")=9;int asm_folded(int n){return n<asm_limit;}\n"
        "static inline int asm_inline(int n)__asm__(\"reason.alias.inlined\");"
        "static inline __attribute__((always_inline)) int asm_inline(int n){return n+9;}int asm_inlined(int n){return asm_inline(n);}\n"
        "static int local_owner(int n)__asm__(\"reason.owner.alias\");"
        "static int local_owner(int n){static int table[2]={3,7};return table[n&1];}"
        "int dead_local_owner(int n){if(0)return local_owner(n);return n;}\n"
        "static int shadow_owner(int n){if(n&1){static int slot[2]={1,2};return slot[n&1];}"
        "if(n&2){static int slot[2]={3,4};return slot[n&1];}if(n&4){static int slot[2]={5,6};return slot[n&1];}"
        "{static int slot[2]={7,8};return slot[n&1];}}int dead_shadow_owner(int n){if(0)return shadow_owner(n);return n;}\n"
        "int retained_owner(int n){static int retained_local[2]={1,3};retained_local[n&1]=n;return retained_local[(n+1)&1];}\n"
        // --symbols verifies emitted helper frames too: sqrt/floor reserve stack space, while copysign is frameless.
        "double helper_sqrt(double x){return __builtin_sqrt(x);}float helper_sqrtf(float x){return __builtin_sqrtf(x);}\n"
        "double helper_floor(double x){return __builtin_floor(x);}float helper_floorf(float x){return __builtin_floorf(x);}\n"
        "double helper_copysign(double x,double y){return __builtin_copysign(x,y);}"
        "float helper_copysignf(float x,float y){return __builtin_copysignf(x,y);}\n"
        "int helper_rounding_mode(void){return __builtin_flt_rounds();}\n";
    char directory[] = "/tmp/buster-symbol-reasons-XXXXXX";
    char path[160];
    char objects[160];
    bool ready = mkdtemp(directory) != 0;
    snprintf(path, sizeof(path), "%s/reasons.c", directory);
    snprintf(objects, sizeof(objects), "%s/objects", directory);
    ready = ready && parse_test_write(path, source, sizeof(source) - 1) && mkdir(objects, 0700) == 0;
    Arena option_arena = arena_reserve(UINT64_C(1) << 24);
    char* text[] = { "-std=gnu17", path };
    PPOptions options;
    const char* input = 0;
    const char* output = 0;
    bool markers = false;
    ready = ready && pp_parse_options(2, text, &options, &option_arena, &input, &output, &markers);
    options.semantic_pragmas = true;
    check_test_expect(test, ready, "--symbols' reasons: the fixture is written and its options read");
    static const char* const same[] = { "-std=gnu17" };
    static const char* const renamed[] = {
        "-std=gnu17", "-Dwide=renamed", "-Dasm_dropped=renamed_dropped", "-Dasm_unselected=renamed_unselected",
        "-Dasm_dropped_data=renamed_dropped_data", "-Dasm_unselected_data=renamed_unselected_data",
        "-Dasm_limit=renamed_limit", "-Dasm_inline=renamed_inline", "-Dretained_local=renamed_retained_local",
    };
    for (uint32_t run = 0; ready && run < 2; run += 1)
    {
        CheckCorpusSymbols symbols = {
            .clang = clang,
            .arguments = run == 0 ? same : renamed,
            .argument_count = run == 0 ? BUSTER_ARRAY_LENGTH(same) : BUSTER_ARRAY_LENGTH(renamed),
            .directory = objects,
        };
        check_corpus_symbols = &symbols;
        CheckCorpusOutcome outcome;
        check_corpus_measure(path, &options, false, true, &outcome);
        check_corpus_symbols = NULL;
        const bool explained = symbols.compared == 1 && symbols.failures == 0 && symbols.documented[CHECK_CORPUS_SYMBOL_DROPPED] == 9 &&
                               symbols.documented[CHECK_CORPUS_SYMBOL_FOLDED] == 2 && symbols.documented[CHECK_CORPUS_SYMBOL_UNSELECTED] == 2 &&
                               symbols.documented[CHECK_CORPUS_SYMBOL_INLINED] == 1 &&
                               symbols.unexplained == (run == 0 ? 4 : 8) && symbols.different == 1;
        check_test_expect(test, explained, run == 0 ? "--symbols preserves explained asm-labelled data and unexplained prototype-labelled function controls" :
                                                      "--symbols preserves stable asm labels under identifier renames and retains the changed emitted function name differences");
        if (!explained)
        {
            printf("--symbols' reasons, run %u: compared=%" PRIu64 " failures=%" PRIu64 " dropped=%" PRIu64 " folded=%" PRIu64 " unselected=%" PRIu64
                   " inlined=%" PRIu64 " unexplained=%" PRIu64 " different=%" PRIu64 "\n", run, symbols.compared, symbols.failures,
                   symbols.documented[CHECK_CORPUS_SYMBOL_DROPPED], symbols.documented[CHECK_CORPUS_SYMBOL_FOLDED],
                   symbols.documented[CHECK_CORPUS_SYMBOL_UNSELECTED], symbols.documented[CHECK_CORPUS_SYMBOL_INLINED], symbols.unexplained,
                   symbols.different);
        }
    }
    arena_release(&option_arena);
    unlink(path);
    rmdir(objects);
    rmdir(directory);
}

#endif
