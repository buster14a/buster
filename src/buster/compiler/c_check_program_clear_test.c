// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1376: an automatic object's zeros past BUSTER_CHECK_LOWER_FILL_UNROLLED bytes are one call of memset, as clang -O0's
// code clears them, rather than a loop of 8-byte stores; a volatile object keeps its stores; an aggregate's copy that
// writes the object whole needs no clear before it; and a compound literal of the object's size initializes the object
// in place, without its own object's clear and copy. #1549: `T x = f(...);` of a struct or union that comes back through
// a hidden pointer passes the object's own address as that pointer, as clang's code does at -O0, rather than an object
// of the call's own whose bytes a COPY (or memmove) then moves. Include after c_check_program_copy_test.c. Clang is the
// oracle of the fixtures' results.

// What a unit's lowering makes of its functions, by each path: whether it calls memset (`clears`) and memmove (`moves`),
// its largest function's typed entries, how many of its jumps and branches go back (a loop's), its COPY entries, and its
// largest function's object bytes (Tier0Function.local_sizes', which a call's result object is one of).
typedef struct CheckTestClearShape CheckTestClearShape;
struct CheckTestClearShape
{
    bool clears;
    bool moves;
    uint32_t entries;
    uint32_t backward;
    uint32_t copies;
    uint32_t objects;
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
        for (uint32_t i = 0; i < function->count; i += 1)
        {
            const uint8_t kind = function->kinds[i];
            const bool branch = kind == BUSTER_TYPED_JUMP || kind == BUSTER_TYPED_BRANCH_TRUE || kind == BUSTER_TYPED_BRANCH_FALSE;
            shape->backward += branch && function->values[i] <= i;
            shape->copies += kind == BUSTER_TYPED_COPY;
        }
        uint32_t objects = 0;
        for (uint32_t l = 0; function->local_sizes != NULL && l < function->local_count; l += 1)
        {
            objects += function->local_sizes[l];
        }
        shape->objects = objects > shape->objects ? objects : shape->objects;
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
    check_test_expect(test, memcmp(shapes, shapes + 1, sizeof(shapes[0])) == 0, description);
}

// #1549: a unit's `T x = f(...);` by both paths, which must agree: whether it copies the call's result (a COPY entry or a
// call of memmove) and its largest function's object bytes, the declared object's alone where the call writes it in
// place (`objects` its size) and the call's own result object's too where a copy moves the result.
static void check_test_program_return_shape(CheckTest* test, const char* source, const char* name, bool copies, uint32_t objects)
{
    CheckTestClearShape shapes[2] = { 0 };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        char description[256];
        snprintf(description, sizeof(description), "%s: lowers and emits by the %s path", name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, check_test_program_clear_lower(source, path == 0, shapes + path), description);
        const bool copied = shapes[path].copies != 0 || shapes[path].moves;
        snprintf(description, sizeof(description), "%s: a copy %u, %u object bytes (%s path: %u COPY entries, memmove %u, %u object bytes)", name, copies,
                 objects, path == 0 ? "scalar" : "vector", shapes[path].copies, shapes[path].moves, shapes[path].objects);
        check_test_expect(test, copied == copies && shapes[path].objects == objects, description);
    }
    char description[192];
    snprintf(description, sizeof(description), "%s: both paths lower the same shape", name);
    check_test_expect(test, memcmp(shapes, shapes + 1, sizeof(shapes[0])) == 0, description);
}

// #1549's fixtures: `T x = f(...);` in place at each size of a hidden-pointer result (past 16 bytes), below and past
// BUSTER_TIER0_MAX_COPY_BYTES, of a struct, a union and an aligned typedef's; and the copies that stay: a 16-byte
// result's (in registers, which the call stores in its own object), an assignment's, a volatile object's and an object
// less aligned than the call's result; then a unit of them run against clang on a stack a call left dirty, with a
// declaration a backward goto reaches again, whose callee reads the object's earlier value through a pointer.
static void check_test_program_returns(CheckTest* test, const char* clang, const char* dirty)
{
    static const uint32_t sizes[] = { 17, 24, 128, 129, 256, 257, 4096 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        char source[512];
        char name[96];
        snprintf(source, sizeof(source), "struct S{unsigned char c[%u];};struct S make(int);int read(const struct S*,int);"
                                         "int returned(int n){struct S s=make(n);return read(&s,n);}", sizes[i]);
        snprintf(name, sizeof(name), "initializer-return-in-place-%u", sizes[i]);
        check_test_program_return_shape(test, source, name, false, sizes[i]);
    }
    check_test_program_return_shape(test, "union U{long l[40];char c[3];};union U make(int);int read(const union U*,int);"
                                          "int returned(int n){union U u=make(n);return read(&u,n);}",
                                    "initializer-return-in-place-union", false, 320);
    check_test_program_return_shape(test, "struct S{long a[40];};typedef struct S __attribute__((aligned(64))) A;A make(int);int read(const A*,int);"
                                          "int returned(int n){A a=make(n);return read(&a,n);}",
                                    "initializer-return-in-place-aligned", false, 320);
    check_test_program_return_shape(test, "struct S{long a[40];int flags;};struct S make(int);int read(const struct S*,int);"
                                          "int returned(int n){const struct S s=make(n);return read(&s,n);}",
                                    "initializer-return-in-place-const", false, 328);
    check_test_program_return_shape(test, "struct S{long x,y;};struct S make(int);int read(const struct S*,int);"
                                          "int returned(int n){struct S s=make(n);return read(&s,n);}",
                                    "initializer-return-registers-copy", true, 32);
    check_test_program_return_shape(test, "struct S{long a[40];int flags;};struct S make(int);int read(const struct S*,int);"
                                          "int returned(int n){struct S s;s=make(n);return read(&s,n);}",
                                    "initializer-return-assignment-copy", true, 656);
    check_test_program_return_shape(test, "struct S{long a[40];int flags;};struct S make(int);int read(const volatile struct S*,int);"
                                          "int returned(int n){volatile struct S s=make(n);return read(&s,n);}",
                                    "initializer-return-volatile-copy", true, 656);
    check_test_program_return_shape(test, "struct S{long a[40];};typedef struct S __attribute__((aligned(64))) A;A make(int);int read(const struct S*,int);"
                                          "int returned(int n){struct S s=make(n);return read(&s,n);}",
                                    "initializer-return-underaligned-copy", true, 640);

    char source[8192];
    snprintf(source, sizeof(source),
             "%s"
             "struct B{long a[40];int flags;};struct T{unsigned char c[17];};union U{long l[20];char c[3];};"
             "typedef struct B __attribute__((aligned(64))) A;struct L{struct L*self;char pad[200];};struct O{struct B in;};"
             "__attribute__((noinline)) static struct B make(int n){struct B b;for(int i=0;i<40;i++)b.a[i]=n+i;b.flags=n^5;return b;}"
             "__attribute__((noinline)) static struct B again(int k,struct B in){struct B b=in;b.flags+=k;b.a[0]=k;return b;}"
             "__attribute__((noinline)) static struct T odd(int n){struct T t;for(int i=0;i<17;i++)t.c[i]=(unsigned char)(n+i);return t;}"
             "__attribute__((noinline)) static union U mixed(int n){union U u;for(int i=0;i<20;i++)u.l[i]=n*i;return u;}"
             "__attribute__((noinline)) static A aligned(int n){A a;for(int i=0;i<40;i++)a.a[i]=n-i;a.flags=n;return a;}"
             "__attribute__((noinline)) static struct L linked(struct L*p,int n){struct L l;l.self=p;for(int i=0;i<200;i++)l.pad[i]=(char)(n+i);return l;}"
             "__attribute__((noinline)) static struct B listed(int n,...){struct B b;for(int i=0;i<40;i++)b.a[i]=n*2+i;b.flags=n;return b;}"
             "static struct B(*volatile pointer)(int)=make;"
             "static long sum(const struct B*b){long s=b->flags;for(int i=0;i<40;i++)s=s*31+b->a[i];return s;}"
             "static int expect(const struct B*b,int n){int bad=b->flags!=(n^5);for(int i=0;i<40;i++)bad|=b->a[i]!=n+i;return bad;}"
             "__attribute__((noinline)) static int plain(int n){struct B b=make(n);int bad=expect(&b,n);b.a[n&31]=n;return bad|(b.a[n&31]!=n);}"
             "__attribute__((noinline)) static int nested(int n){struct B b=again(make(n).flags,make(n+1));"
             "return b.a[0]!=(n^5)||b.flags!=((n+1)^5)+(n^5)||b.a[1]!=n+2;}"
             "__attribute__((noinline)) static int looped(int n){int bad=0;for(int k=0;k<4;k++){struct B b=make(n+k);bad|=expect(&b,n+k);b.flags=-1;b.a[3]=0;}return bad;}"
             "__attribute__((noinline)) static int jumped(int n){int bad=0;int k=0;top:;struct B b=make(n+k);bad|=expect(&b,n+k);b.a[7]=-1;if(++k<3)goto top;return bad;}"
             "__attribute__((noinline)) static struct B carry(const struct B*p,int n){long old=p?p->a[1]:-1;struct B b;for(int i=0;i<40;i++)b.a[i]=n+i;"
             "b.flags=(int)old;return b;}"
             "__attribute__((noinline)) static int revisited(int n){const struct B*p=0;int k=0;int bad=0;top:;struct B b=carry(p,n+k);"
             "bad|=b.flags!=(p?n+k:-1)||b.a[39]!=n+k+39;p=&b;if(++k<3)goto top;return bad;}"
             "__attribute__((noinline)) static int self(int n){struct L l=linked(&l,n);int bad=l.self!=&l;for(int i=0;i<200;i++)bad|=l.pad[i]!=(char)(n+i);return bad;}"
             "__attribute__((noinline)) static int through(int n){struct B b=pointer(n);return expect(&b,n);}"
             "__attribute__((noinline)) static int variadic(int n){struct B b=listed(n,1.5,n,\"x\");return b.flags!=n||b.a[39]!=n*2+39;}"
             "__attribute__((noinline)) static int short_(int n){struct T t=odd(n);int bad=0;for(int i=0;i<17;i++)bad|=t.c[i]!=(unsigned char)(n+i);return bad;}"
             "__attribute__((noinline)) static int unions(int n){union U u=mixed(n);int bad=0;for(int i=0;i<20;i++)bad|=u.l[i]!=n*i;return bad;}"
             "__attribute__((noinline)) static int realigned(int n){A a=aligned(n);int bad=((unsigned long)&a&63)!=0||a.flags!=n;"
             "for(int i=0;i<40;i++)bad|=a.a[i]!=n-i;struct B b=aligned(n+1);return bad|(b.a[2]!=n-1);}"
             "__attribute__((noinline)) static int wrapped(int n){struct B one[1]={make(n)};struct O o={make(n+1)};return expect(one,n)|expect(&o.in,n+1);}"
             "__attribute__((noinline)) static int kept(int n){struct B b;b=make(n);b=again(b.a[2],b);volatile struct B v=make(n+2);"
             "return b.a[0]!=n+2||b.flags!=(n^5)+n+2||v.flags!=((n+2)^5)||v.a[39]!=n+41;}"
             "int tested(int n){int bad=0;dirty(n);bad|=plain(n);dirty(n);bad|=nested(n)<<1;dirty(n);bad|=looped(n)<<2;dirty(n);bad|=jumped(n)<<3;"
             "dirty(n);bad|=self(n)<<4;dirty(n);bad|=through(n)<<5;dirty(n);bad|=variadic(n)<<6;dirty(n);bad|=short_(n)<<7;dirty(n);bad|=unions(n)<<8;"
             "dirty(n);bad|=realigned(n)<<9;dirty(n);bad|=wrapped(n)<<10;dirty(n);bad|=kept(n)<<11;dirty(n);bad|=revisited(n)<<12;struct B s=make(n);return bad?123456+bad:(int)(sum(&s)&0xffff);}",
             dirty);
    check_test_program_source(test, source, "initializer-returns-in-place-on-dirty-stack", clang);
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
    check_test_program_returns(test, clang, dirty);
}
