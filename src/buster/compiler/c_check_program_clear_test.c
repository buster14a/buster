// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1376: an automatic object's zeros past BUSTER_CHECK_LOWER_FILL_UNROLLED bytes are one call of memset, as clang -O0's
// code clears them, rather than a loop of 8-byte stores; a volatile object keeps its stores; an aggregate's copy that
// writes the object whole needs no clear before it; and a compound literal of the object's size initializes the object
// in place, without its own object's clear and copy. #1549 also lets an unaliased whole-object call initializer use
// the declared object as its hidden result. Include after c_check_program_copy_test.c. Clang is the result oracle.

// What a unit's lowering makes of its functions, by each path: whether it calls memset (`clears`) and memmove (`moves`),
// its largest function's typed entries and object bytes, its inline copies, the addresses it discards as soon as it
// makes them (an in-place call's dead lea before #1549's follow-up), and how many jumps go back (a loop's).
typedef struct CheckTestClearShape CheckTestClearShape;
struct CheckTestClearShape
{
    bool clears;
    bool moves;
    uint32_t entries;
    uint32_t backward;
    uint32_t copies;
    uint32_t dead_addresses;
    uint64_t object_bytes;
};

static bool check_test_program_clear_lower(const char* source, bool scalar, CheckTestClearShape* shape)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions preprocessing = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &preprocessing, &identifiers) &&
                 pp_run_source(&pp, "initializer-clear-shape", (const uint8_t*)source, (uint32_t)strlen(source));
    CheckTestTypes types = { 0 };
    Arena input_arena = arena_reserve(UINT64_C(1) << 26);
    Arena output = arena_reserve(UINT64_C(1) << 26);
    Arena bytes = arena_reserve(UINT64_C(1) << 22);
    Arena scratch = arena_reserve(UINT64_C(1) << 26);
    const ParseOptions parsing = { .scalar = scalar };
    const CheckOptions checking = { .scalar = scalar };
    CheckInput input = { 0 };
    CheckStream stream = { 0 };
    CheckInitializers plans = { 0 };
    CheckAsms asms = { 0 };
    CheckProgram program = { 0 };
    CheckProgramCode code = { 0 };
    ready = ready && input_arena.base != 0 && output.base != 0 && bytes.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
            parse_initialize(&parser, &parsing, preprocessing.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
            check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
            check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
            check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
            check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
    const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = scalar };
    ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &input_arena, &scratch) &&
            check_program_emit(&code, &program, &parser, &identifiers, (LinkCapacity){ 0 }, scalar, &output, &bytes, &scratch, NULL, NULL, false, true);
    *shape = (CheckTestClearShape){
        .clears = ready && code.lowered.runtime_symbols[CHECK_LOWER_RUNTIME_MEMSET] != 0,
        .moves = ready && code.lowered.runtime_symbols[CHECK_LOWER_RUNTIME_MEMMOVE] != 0,
    };
    for (uint32_t b = 0; ready && b < code.lowered.count; b += 1)
    {
        const Tier0Function* function = &code.lowered.functions[b];
        shape->entries = function->count > shape->entries ? function->count : shape->entries;
        uint64_t object_bytes = 0;
        for (uint32_t local = 0; function->local_sizes != NULL && local < function->local_count; local += 1)
        {
            object_bytes += function->local_sizes[local];
        }
        shape->object_bytes = object_bytes > shape->object_bytes ? object_bytes : shape->object_bytes;
        for (uint32_t i = 0; i < function->count; i += 1)
        {
            const uint8_t kind = function->kinds[i];
            const bool branch = kind == BUSTER_TYPED_JUMP || kind == BUSTER_TYPED_BRANCH_TRUE || kind == BUSTER_TYPED_BRANCH_FALSE;
            shape->backward += branch && function->values[i] <= i;
            shape->copies += kind == BUSTER_TYPED_COPY;
            shape->dead_addresses += kind == BUSTER_TYPED_DISCARD && i != 0 && function->kinds[i - 1] == BUSTER_TYPED_LOCAL_ADDRESS;
        }
    }
    parse_release(&parser);
    check_test_types_close(&types);
    arena_release(&scratch);
    arena_release(&bytes);
    arena_release(&output);
    arena_release(&input_arena);
    pp_release(&pp);
    intern_release(&identifiers);
    return ready;
}

// A unit's shape by both paths, which must agree, against the expected memset and memmove calls and loop, and at most
// `entries` typed entries in any function.
static void check_test_program_clear_shape(CheckTest* test, const char* source, const char* name, bool clears, bool moves, bool loops, uint32_t entries)
{
    CheckTestClearShape shapes[2] = { 0 };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        char description[256];
        snprintf(description, sizeof(description), "%s: lowers and emits by the %s path", name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, check_test_program_clear_lower(source, path == 0, shapes + path), description);
        snprintf(description, sizeof(description), "%s: memset %u, memmove %u, a loop %u, at most %u typed entries (%s path: %u, %u, %u back, %u entries)", name,
                 clears, moves, loops, entries, path == 0 ? "scalar" : "vector", shapes[path].clears, shapes[path].moves, shapes[path].backward,
                 shapes[path].entries);
        check_test_expect(test, shapes[path].clears == clears && shapes[path].moves == moves && (shapes[path].backward != 0) == loops &&
                                    shapes[path].entries <= entries,
                          description);
    }
    char description[192];
    snprintf(description, sizeof(description), "%s: both paths lower the same shape", name);
    check_test_expect(test, shapes[0].clears == shapes[1].clears && shapes[0].moves == shapes[1].moves && shapes[0].entries == shapes[1].entries &&
                                shapes[0].backward == shapes[1].backward && shapes[0].copies == shapes[1].copies && shapes[0].object_bytes == shapes[1].object_bytes,
                      description);
}

// The result initialization must remove both the copy and its temporary, while the excluded paths retain them.
// Each fixture has one defined function, so the object's bytes also distinguish a nested call's separate result. A
// fixture without a copy writes in place, whose call leaves no address to discard either.
static void check_test_program_call_initializer_shape(CheckTest* test, const char* source, const char* name, uint32_t copies, bool moves, uint64_t bytes)
{
    const bool placed = copies == 0 && !moves;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CheckTestClearShape shape = { 0 };
        char description[320];
        const bool lowered = check_test_program_clear_lower(source, path == 0, &shape);
        snprintf(description, sizeof(description),
                 "%s: %s path emits %u inline copies, memmove %u, %" PRIu64 " object bytes and %s discarded addresses (got %u, %u, %" PRIu64 ", %u)", name,
                 path == 0 ? "scalar" : "vector", copies, moves, bytes, placed ? "no" : "any", shape.copies, shape.moves, shape.object_bytes, shape.dead_addresses);
        check_test_expect(test, lowered && shape.copies == copies && shape.moves == moves && shape.object_bytes == bytes && (!placed || shape.dead_addresses == 0),
                          description);
    }
}

static void check_test_program_clears(CheckTest* test, const char* clang)
{
    // The threshold: up to BUSTER_CHECK_LOWER_FILL_UNROLLED bytes are stores, past it one call whatever the size.
    static const uint32_t sizes[] = { 127, 128, 129, 136, 1024, 65536, 1048576 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        char source[512];
        char name[96];
        const bool called = sizes[i] > BUSTER_CHECK_LOWER_FILL_UNROLLED;
        snprintf(source, sizeof(source), "struct S{unsigned char c[%u];};int read(const struct S*,int);"
                                         "int zeros(int n){struct S s={0};return read(&s,n);}"
                                         "int first(int n){struct S s={(unsigned char)n};return read(&s,n);}"
                                         "int last(int n){struct S s={.c[%u]=(unsigned char)n};return read(&s,n);}", sizes[i], sizes[i] - 1);
        snprintf(name, sizeof(name), "initializer-clear-%u", sizes[i]);
        check_test_program_clear_shape(test, source, name, called, false, false, called ? 32 : 128);
    }
    // A volatile object keeps its stores, the loop's past the threshold; so does one with a volatile member.
    check_test_program_clear_shape(test, "int read(const volatile int*,int);int f(int n){volatile int a[256]={n};return read(a,n);}",
                                   "initializer-clear-volatile", false, false, true, UINT32_MAX);
    check_test_program_clear_shape(test, "struct S{int a[255];volatile int v;};int read(const struct S*,int);int f(int n){struct S s={n};return read(&s,n);}",
                                   "initializer-clear-volatile-member", false, false, true, UINT32_MAX);
    // An aggregate's copy that writes the object whole takes no clear first: no memset, no stores of zeros (a clear of 128
    // bytes is 64 entries), at every size.
    static const uint32_t copies[] = { 16, 128, 129, 256, 257, 4096 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(copies); i += 1)
    {
        char source[512];
        char name[96];
        snprintf(source, sizeof(source), "struct S{unsigned char c[%u];};struct S make(int);int read(const struct S*,int);"
                                         "int copied(const struct S*p,int n){struct S s=*p;return read(&s,n);}"
                                         "int returned(int n){struct S s=make(n);return read(&s,n);}", copies[i]);
        snprintf(name, sizeof(name), "initializer-copy-%u", copies[i]);
        check_test_program_clear_shape(test, source, name, false, copies[i] > BUSTER_TIER0_MAX_COPY_BYTES, false, 32);
    }
    // Hidden-pointer returns use the declared object directly on both sides of the inline-copy threshold. The small
    // register returns keep their previous result object and copy; this change makes no ABI classification decision.
    static const uint32_t returns[] = { 1, 8, 16, 17, 24, 128, 256, 257, 4096 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(returns); i += 1)
    {
        char source[512];
        char name[96];
        const uint32_t size = returns[i];
        snprintf(source, sizeof(source), "struct S{unsigned char c[%u];};struct S make(int);int read(const struct S*);"
                                         "int f(int n){struct S s=make(n);return read(&s);}", size);
        snprintf(name, sizeof(name), "initializer-call-result-%u", size);
        check_test_program_call_initializer_shape(test, source, name, size <= 16, false, (uint64_t)size * (size <= 16 ? 2 : 1));
    }
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(struct S);struct S first(int);int read(const struct S*);"
                                                    "int f(int n){struct S s=make(first(n));return read(&s);}",
                                              "initializer-call-nested-result", 0, false, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};int read(const struct S*);"
                                                    "int f(struct S(*make)(int),int n){struct S s=make(n);return read(&s);}",
                                              "initializer-call-indirect-result", 0, false, 300);
    check_test_program_call_initializer_shape(test, "union U{char c[24];long n;};union U make(int);int read(const union U*);"
                                                    "int f(int n){union U u=make(n);return read(&u);}",
                                              "initializer-call-union-result", 0, false, 24);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){_Alignas(64) struct S s=make(n);return read(&s);}",
                                              "initializer-call-overaligned-result", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{_Alignas(16) long a[4];};typedef struct S Low __attribute__((aligned(1)));"
                                                    "struct S make(int);int read(const Low*);int f(int n){Low s=make(n);return read(&s);}",
                                              "initializer-call-canonical-alignment", 1, false, 64);
    check_test_program_call_initializer_shape(test, "struct S{char c[24];};struct S make(int);int read(const volatile struct S*);"
                                                    "int f(int n){volatile struct S s=make(n);return read(&s);}",
                                              "initializer-call-volatile-result", 1, false, 48);
    check_test_program_call_initializer_shape(test, "struct S{volatile char c[24];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){struct S s=make(n);return read(&s);}",
                                              "initializer-call-volatile-member", 1, false, 48);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int,struct S*);int read(const struct S*);"
                                                    "int f(int n){struct S s=make(n+1+2+3+4+5+6+7+8+9+10+11+12+13+14+15+16,&s);return read(&s);}",
                                              "initializer-call-self-reference-vector-tail", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int publish(struct S*);int read(const struct S*);"
                                                    "int f(void){struct S s=make(publish(&s));return read(&s);}",
                                              "initializer-call-self-reference-nested", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){struct S s;s=make(n);return read(&s);}",
                                              "assignment-call-keeps-copy", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){again:;struct S s=make(n);if(n-->0)goto again;return read(&s);}",
                                              "initializer-call-reentered-keeps-copy", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);void*b[5];"
                                                    "int f(int n){int r=__builtin_setjmp(b);struct S s=make(n+r);return read(&s);}",
                                              "initializer-call-after-setjmp-keeps-copy", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);void*b[5];"
                                                    "int f(void){struct S s=make(__builtin_setjmp(b));return read(&s);}",
                                              "initializer-call-nested-setjmp-keeps-copy", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "typedef long jmp_buf[8];int setjmp(jmp_buf);jmp_buf b;"
                                                    "int f(int n){if(setjmp(b))n=3;struct S s=make(n);return read(&s);}",
                                              "initializer-call-library-setjmp-keeps-copy", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int resume(void)__attribute__((returns_twice));"
                                                    "int f(void){int n=resume();struct S s=make(n);return read(&s);}",
                                              "initializer-call-after-returns-twice-keeps-copy", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int resume(void);int resume(void)__attribute__((returns_twice));int resume(void);"
                                                    "int f(void){struct S s=make(resume());return read(&s);}",
                                              "initializer-call-redeclared-returns-twice-keeps-copy", 0, true, 600);
    // Labels: only one between the opener of the declaration's innermost group and the initializer's end can run the
    // declaration again in the object's lifetime. A cleanup label after it, a label outside its block (whose entry
    // begins a new lifetime) and a for statement's body's label, past its header's declaration, leave it in place; a
    // label before it in its own block, in a nested block too, keeps the copy. The long statements put the label, or the
    // group's opener, past one vector of tokens from the initializer, so the vector path finds them in a later block.
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){struct S s=make(n);if(n<0)goto out;n=read(&s);out:return n;}",
                                              "initializer-call-cleanup-label-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){again:;if(n-->5)goto again;{struct S s=make(n);return read(&s);}}",
                                              "initializer-call-enclosing-label-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){int r=0;for(int i=0;i<n;i++){if(i==2)goto skip;r+=i;skip:;}"
                                                    "{struct S s=make(n);r+=read(&s);}return r;}",
                                              "initializer-call-sibling-block-label-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){int r=0;again:r++;if(r<3)goto again;for(int i=0;i<n;i++){struct S s=make(i);r+=read(&s);}return r;}",
                                              "initializer-call-loop-body-after-label-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){int r=0;for(struct S s=make(n);r<3;r++){if(r==1)goto next;r+=read(&s);next:;}return r;}",
                                              "initializer-call-for-header-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){again:;if(n-->5)goto again;"
                                                    "{int k=n+1+2+3+4+5+6+7+8+9+10+11+12+13+14+15+16;struct S s=make(k+1+2+3+4+5+6+7+8);return read(&s);}}",
                                              "initializer-call-enclosing-label-vector-in-place", 0, false, 300);
    // One vector from the initializer's end starts at the enclosing label's colon, two tokens before the block's opener:
    // the nearer, the opener, decides.
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){again:;{struct S s=make(~n+1+2);if(n-->5)goto again;return read(&s);}}",
                                              "initializer-call-label-and-opener-in-one-vector-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){again:;n=n+1+2+3+4+5+6+7+8+9+10+11+12+13+14+15+16;"
                                                    "struct S s=make(n+1+2+3+4+5+6+7+8);if(n<100)goto again;return read(&s);}",
                                              "initializer-call-earlier-label-vector-keeps-copy", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){{again:;n--;}struct S s=make(n);if(n>0)goto again;return read(&s);}",
                                              "initializer-call-nested-earlier-label-keeps-copy", 0, true, 600);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){int r=0;for(int i=0;i<n;i++){if(i==2)goto skip;r+=i;skip:;}struct S s=make(n);return r+read(&s);}",
                                              "initializer-call-earlier-loop-label-keeps-copy", 0, true, 600);
    // The part the call writes may be the object's only member or element, of the object's size: its type is the
    // result's. A wider object, or a volatile member, keeps the copy.
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct O{struct S s;};struct S make(int);int read(const struct O*);"
                                                    "int f(int n){struct O o={make(n)};return read(&o);}",
                                              "initializer-call-member-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct O{struct S s;};struct S make(int);int read(const struct O*);"
                                                    "int f(int n){struct O o={.s=make(n)};return read(&o);}",
                                              "initializer-call-designated-member-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct O{struct S s;};struct P{struct O o;};struct S make(int);"
                                                    "int read(const struct P*);int f(int n){struct P p={{make(n)}};return read(&p);}",
                                              "initializer-call-nested-member-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct S make(int);int read(const struct S*);"
                                                    "int f(int n){struct S a[1]={make(n)};return read(a);}",
                                              "initializer-call-element-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};union W{struct S s;char c[300];};struct S make(int);int read(const union W*);"
                                                    "int f(int n){union W w={make(n)};return read(&w);}",
                                              "initializer-call-union-member-in-place", 0, false, 300);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct O{struct S s;int extra;};struct S make(int);int read(const struct O*);"
                                                    "int f(int n){struct O o={make(n)};return read(&o);}",
                                              "initializer-call-wider-member-keeps-copy", 0, true, 604);
    check_test_program_call_initializer_shape(test, "struct S{char c[300];};struct O{volatile struct S s;};struct S make(int);int read(const struct O*);"
                                                    "int f(int n){struct O o={make(n)};return read(&o);}",
                                              "initializer-call-volatile-member-object-keeps-copy", 2, false, 600);
    // A union's member smaller than the union leaves bytes the clear writes.
    check_test_program_clear_shape(test, "struct M{char c[200];};union U{struct M m;long l[40];};int read(const union U*,int);"
                                         "int f(const struct M*p,int n){union U u={*p};return read(&u,n);}",
                                   "initializer-copy-smaller-member", true, false, false, 32);
    // A compound literal of the object's size initializes it in place: its zeros the object's one memset, no copy, also
    // two literals deep.
    check_test_program_clear_shape(test, "struct S{long a[40];int flags;};int read(const struct S*,int);"
                                         "int f(int n){struct S s=(struct S){.flags=n};return read(&s,n);}",
                                   "initializer-literal-in-place", true, false, false, 32);
    check_test_program_clear_shape(test, "struct I{int v[100];};struct O{struct I in;};int read(const struct O*,int);"
                                         "int f(int n){struct O o=(struct O){(struct I){.v[3]=n}};return read(&o,n);}",
                                   "initializer-literal-in-place-nested", true, false, false, 32);
    // A volatile object is cleared by its stores, then the literal's copy (the literal's own zeros memset's) written into it
    // a piece at a time, as a volatile copy is.
    check_test_program_clear_shape(test, "struct S{long a[40];int flags;};int read(const volatile struct S*,int);"
                                         "int f(int n){volatile struct S s=(struct S){.flags=n};return read(&s,n);}",
                                   "initializer-literal-volatile-object", true, false, true, UINT32_MAX);

    // The fixtures' objects are cleared on a stack a call left dirty, so that a clear left out shows.
    static const char dirty[] = "__attribute__((noinline)) static void dirty(int n){volatile unsigned char d[16384];"
                                "for(int i=0;i<16384;i++)d[i]=(unsigned char)(n+i*7+1);}";
    static const uint32_t execution_sizes[] = { 127, 128, 129, 136, 1000, 4097 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(execution_sizes); i += 1)
    {
        const uint32_t size = execution_sizes[i];
        char source[4096];
        char name[96];
        snprintf(source, sizeof(source),
                 "%s\n#define Z %u\n"
                 "struct S{unsigned char c[Z];};struct P{int x;unsigned char gap[Z];int y;};union U{unsigned char c[3];struct S s;};"
                 "__attribute__((noinline)) static int zeros(int n){struct S s={0};int bad=0;for(int i=0;i<Z;i++)bad|=s.c[i];return bad;}"
                 "__attribute__((noinline)) static int first(int n){struct S s={(unsigned char)n,2};int bad=s.c[0]!=(unsigned char)n||s.c[1]!=2;"
                 "for(int i=2;i<Z;i++)bad|=s.c[i];return bad;}"
                 "__attribute__((noinline)) static int last(int n){struct S s={.c[Z-1]=(unsigned char)(n|1)};int bad=s.c[Z-1]!=(unsigned char)(n|1);"
                 "for(int i=0;i<Z-1;i++)bad|=s.c[i];return bad;}"
                 "__attribute__((noinline)) static int gaps(int n){struct P p={.x=n,.y=n+1};int bad=p.x!=n||p.y!=n+1;"
                 "for(int i=0;i<Z;i++)bad|=p.gap[i];return bad;}"
                 "__attribute__((noinline)) static int text(int n){char t[Z]=\"ab\";int bad=t[0]!='a'||t[1]!='b';for(int i=2;i<Z;i++)bad|=t[i];"
                 "t[(unsigned)n%%Z]=1;return bad;}"
                 "__attribute__((noinline)) static int unions(int n){union U u={{1,2,(unsigned char)n}};int bad=u.c[2]!=(unsigned char)n;"
                 "for(int i=3;i<Z;i++)bad|=u.s.c[i];return bad;}"
                 "__attribute__((noinline)) static int vol(int n){volatile struct S s={.c[1]=(unsigned char)n};int bad=s.c[1]!=(unsigned char)n;"
                 "for(int i=2;i<Z;i++)bad|=s.c[i];return bad;}"
                 "__attribute__((noinline)) static int loop(int n){int bad=0;for(int k=0;k<3;k++){struct S s={(unsigned char)k};"
                 "bad|=s.c[0]!=(unsigned char)k||s.c[Z-1];s.c[Z-1]=(unsigned char)(n+1);}return bad;}"
                 "int tested(int n){int bad=0;dirty(n);bad|=zeros(n);dirty(n);bad|=first(n)<<1;dirty(n);bad|=last(n)<<2;dirty(n);bad|=gaps(n)<<3;"
                 "dirty(n);bad|=text(n)<<4;dirty(n);bad|=unions(n)<<5;dirty(n);bad|=vol(n)<<6;dirty(n);bad|=loop(n)<<7;return bad?123456+bad:n;}",
                 dirty, size);
        snprintf(name, sizeof(name), "initializer-clears-on-dirty-stack-%u", size);
        check_test_program_source(test, source, name, clang);
    }
    char source[4096];
    snprintf(source, sizeof(source),
             "%s"
             "struct S{unsigned char c[300];};struct B{long a[40];int flags;};struct I{int v[100];};struct O{struct I in;};"
             "struct L{struct L*self;char pad[200];};struct M{char c[200];};union W{struct M m;long l[40];};"
             "static struct S global;struct S make(int n){struct S s;for(int i=0;i<300;i++)s.c[i]=(unsigned char)(n+i);return s;}"
             "__attribute__((noinline)) static int copied(int n){struct S s=global;int bad=0;for(int i=0;i<300;i++)bad|=s.c[i]!=global.c[i];return bad;}"
             "__attribute__((noinline)) static int returned(int n){struct S s=make(n);int bad=0;for(int i=0;i<300;i++)bad|=s.c[i]!=(unsigned char)(n+i);"
             "return bad;}"
             "__attribute__((noinline)) static int literal(int n){struct B b=(struct B){.flags=n,.a[5]=n+1};int bad=b.flags!=n||b.a[5]!=n+1;"
             "for(int i=0;i<40;i++)bad|=i!=5&&b.a[i]!=0;return bad;}"
             "__attribute__((noinline)) static int nested(int n){struct O o=(struct O){(struct I){.v[3]=n}};int bad=o.in.v[3]!=n;"
             "for(int i=0;i<100;i++)bad|=i!=3&&o.in.v[i]!=0;return bad;}"
             "__attribute__((noinline)) static int self(int n){struct L l=(struct L){.self=&l,.pad[7]=(char)(n|1)};int bad=l.self!=&l||l.pad[7]!=(char)(n|1);"
             "for(int i=0;i<200;i++)bad|=i!=7&&l.pad[i];return bad;}"
             "__attribute__((noinline)) static int smaller(int n){struct M m;for(int i=0;i<200;i++)m.c[i]=(char)(n+i);union W w={m};int bad=0;"
             "for(int i=0;i<200;i++)bad|=w.m.c[i]!=(char)(n+i);for(int i=25;i<40;i++)bad|=w.l[i]!=0;return bad;}"
             "__attribute__((noinline)) static int constant(int n){const struct B b=(const struct B){.a={n,n,n}};int bad=b.a[0]!=n||b.a[2]!=n;"
             "for(int i=3;i<40;i++)bad|=b.a[i]!=0;return bad|b.flags;}"
             "__attribute__((noinline)) static int volatiles(int n){volatile struct B b=(struct B){.flags=n};int bad=b.flags!=n;"
             "for(int i=0;i<40;i++)bad|=b.a[i]!=0;return bad;}"
             "int tested(int n){for(int i=0;i<300;i++)global.c[i]=(unsigned char)(n*3+i);int bad=0;dirty(n);bad|=copied(n);dirty(n);"
             "bad|=returned(n)<<1;dirty(n);bad|=literal(n)<<2;dirty(n);bad|=nested(n)<<3;dirty(n);bad|=self(n)<<4;dirty(n);bad|=smaller(n)<<5;"
             "dirty(n);bad|=constant(n)<<6;dirty(n);bad|=volatiles(n)<<7;return bad?123456+bad:n;}",
             dirty);
    check_test_program_source(test, source, "initializer-copies-and-literals-on-dirty-stack", clang);
    check_test_program_source(test,
        "struct S{long a[40];int flags;};static int calls;"
        "static struct S make(int n){struct S s;for(int i=0;i<40;i++)s.a[i]=n+i;s.flags=n;calls=calls*3+1;return s;}"
        "static struct S change(struct S s,int n){s.a[3]+=n;s.flags+=n;calls=calls*3+2;return s;}"
        "static struct S(*choose(int n))(int){calls=calls*3+(n&1);return make;}"
        "static struct S self(struct S*p,int n){p->flags=n+9;struct S s=make(p->flags);return s;}"
        "int tested(int n){calls=0;struct S a=change(make(n),n+1);struct S b=choose(n)(n+2);"
        "_Alignas(64) struct S c=make(n+3);volatile struct S v=make(n+4);struct S d=self(&d,n+5);"
        "struct S e=make(n+6);e=change(e,n+7);"
        "int bad=a.a[3]!=2L*n+4||a.flags!=2*n+1||b.a[39]!=n+41L||b.flags!=n+2||"
        "c.a[0]!=n+3L||((unsigned long)&c&63)!=0||v.a[39]!=n+43L||v.flags!=n+4||d.flags!=n+14||"
        "e.a[3]!=2L*n+16||e.flags!=2*n+13;return bad?123456+bad:calls;}",
        "initializer-call-nesting-aliases-and-alignment", clang);
    // Members, elements, labels around in-place calls, and a backward goto whose callee's argument reads the earlier
    // visit's object through an escaped pointer, which keeps its copy.
    check_test_program_source(test,
        "struct S{long a[40];int flags;};struct O{struct S s;};union W{struct S s;long l[41];};static int calls;"
        "static struct S make(int n){struct S s;for(int i=0;i<40;i++)s.a[i]=n+i;s.flags=n;calls=calls*3+1;return s;}"
        "static long sum(const struct S*s){long t=s->flags;for(int i=0;i<40;i++)t+=s->a[i]*(i+1);return t;}"
        "int tested(int n){calls=0;long r=0;struct O o={make(n)};r+=sum(&o.s);struct S a[1]={make(n+1)};r+=sum(a);"
        "union W w={make(n+2)};r+=sum(&w.s);struct O d={.s=make(n+3)};r+=sum(&d.s)*2;"
        "for(int i=0;i<3;i++){if(i==1)goto skip;r+=i;skip:;}{struct S x=make(n+4);r+=sum(&x)*3;}"
        "for(struct S y=make(n+5);y.flags<n+8;y.flags++){if(y.flags==n+6)goto next;r+=sum(&y);next:;}"
        "struct S*seen=0;int visits=0;again:;struct S b=make(seen!=0?(int)seen->a[1]+1:n);seen=&b;if(++visits<3)goto again;"
        "r+=sum(&b)*5;if(n<-1000)goto out;{struct S z=make(n+9);r+=sum(&z)*7;}out:return (int)(r%1000003)+calls%1009;}",
        "initializer-call-members-labels-and-reentry", clang);
}
