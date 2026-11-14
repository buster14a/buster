// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2684: a call whose result is the function's own, through the hidden pointer, passes that pointer on, as clang -O0's
// code passes its sret pointer on: `return f(...);` and the named result's (#1375) call initializer. Its callee writes
// the result where the function's caller wants it: no inline copy, no memmove and no object for the result, by both
// paths. The excluded shapes keep their object and their copy. Include after c_check_program_named_result_test.c (and
// c_check_program_clear_test.c, whose shape helper counts a unit's forwarded calls, inline copies, memmove and object
// bytes). Clang is the result oracle.

// A unit's shape by both paths: its forwarded calls (BUSTER_TIER0_RESULT_HIDDEN), inline copies, memmove and largest
// function's object bytes; one without a copy leaves no address to discard either.
static void check_test_program_forward_shape(CheckTest* test, const char* source, const char* name, uint32_t forwarded, uint32_t copies, bool moves,
                                             uint64_t bytes)
{
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CheckTestClearShape shape = { 0 };
        char description[384];
        const bool lowered = check_test_program_clear_lower(source, path == 0, &shape);
        snprintf(description, sizeof(description),
                 "%s: %s path forwards %u calls and emits %u inline copies, memmove %u and %" PRIu64 " object bytes (got %u, %u, %u, %" PRIu64 ", %u discarded addresses)",
                 name, path == 0 ? "scalar" : "vector", forwarded, copies, moves, bytes, shape.forwarded, shape.copies, shape.moves, shape.object_bytes,
                 shape.dead_addresses);
        check_test_expect(test, lowered && shape.forwarded == forwarded && shape.copies == copies && shape.moves == moves && shape.object_bytes == bytes &&
                                    (copies != 0 || moves || shape.dead_addresses == 0),
                          description);
    }
}

static void check_test_program_forwarded_results(CheckTest* test, const char* clang)
{
    // Both sides of the inline copy's limit (BUSTER_TIER0_MAX_COPY_BYTES, past which a copy is memmove's).
    static const uint32_t sizes[] = { 17, 24, 64, 256, 257, 4096 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        char source[512];
        char name[96];
        snprintf(source, sizeof(source), "struct S{unsigned char c[%u];};struct S make(int);struct S f(int n){return make(n+1);}", sizes[i]);
        snprintf(name, sizeof(name), "forwarded-return-%u", sizes[i]);
        check_test_program_forward_shape(test, source, name, 1, 0, false, 0);
        snprintf(source, sizeof(source), "struct S{unsigned char c[%u];};struct S make(int);struct S f(int n){struct S s=make(n);s.c[0]+=1;return s;}", sizes[i]);
        snprintf(name, sizeof(name), "forwarded-named-result-%u", sizes[i]);
        check_test_program_forward_shape(test, source, name, 1, 0, false, 0);
    }
    check_test_program_forward_shape(test, "union U{unsigned char c[64];long l;};union U make(int);union U f(int n){return make(n);}", "forwarded-union", 1, 0, false, 0);
    // Through the conversions and the selections around the call, and through a pointer.
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return (make(n));}", "forwarded-parenthesized", 1, 0, false, 0);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return _Generic(n,int:make(n),default:make(0));}",
                                     "forwarded-generic", 1, 0, false, 0);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return __builtin_choose_expr(1,make(n),make(0));}",
                                     "forwarded-choose", 1, 0, false, 0);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return __extension__ make(n);}", "forwarded-extension", 1, 0, false,
                                     0);
    check_test_program_forward_shape(test, "struct S{long a[8];};const struct S make(int);struct S f(int n){return make(n);}", "forwarded-qualified-result", 1, 0,
                                     false, 0);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S f(struct S(*make)(int),int n){return make(n);}", "forwarded-indirect", 1, 0, false, 0);
    check_test_program_forward_shape(test, "struct S{long a[8];};typedef struct S A __attribute__((aligned(32)));A make(int);struct S f(int n){return make(n);}",
                                     "forwarded-aligned-typedef", 1, 0, false, 0);
    // Every return of a call forwards; an argument's call keeps its own object, as #1549's nested calls do.
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S other(int);struct S f(int n){if(n)return make(n);return other(n);}",
                                     "forwarded-two-returns", 2, 0, false, 0);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S change(struct S,int);struct S f(int n){return change(make(n),n);}",
                                     "forwarded-nested-argument", 1, 0, false, 64);
    // A return of another object keeps its copy beside a forwarded one, as no object is the named result.
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);void fill(struct S*,int);"
                                           "struct S f(int n){struct S s;fill(&s,n);if(n)return s;return make(n);}",
                                     "forwarded-beside-a-copy", 1, 1, false, 64);
    // The named result's address may escape before its initializer's call runs only through the initializer, which
    // keeps the call's own object then (check_lower_init_names_destination).
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(struct S*,int);struct S f(int n){struct S s=make(&s,n);return s;}",
                                     "forwarded-named-result-self-reference", 0, 1, false, 64);

    // Every other shape keeps its object and its copy, or has neither.
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);void done(int*);"
                                           "struct S f(int n){__attribute__((cleanup(done)))int guard=n;return make(n);}",
                                     "forwarded-kept-before-cleanup", 0, 1, false, 64);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);void done(int*);"
                                           "struct S f(int n){if(n>3){__attribute__((cleanup(done)))int guard=n;return make(guard);}return make(n);}",
                                     "forwarded-body-with-a-kept-return", 0, 2, false, 128);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);void*b[5];struct S f(int n){int r=__builtin_setjmp(b);return make(n+r);}",
                                     "forwarded-setjmp", 0, 1, false, 64);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int)__attribute__((returns_twice));struct S f(int n){return make(n);}",
                                     "forwarded-returns-twice", 0, 1, false, 64);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return n?make(n):make(-n);}", "forwarded-conditional", 0, 1,
                                     false, 128);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return (n++,make(n));}", "forwarded-comma", 0, 1, false, 64);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){__attribute__((musttail))return make(n);}", "forwarded-musttail",
                                     0, 0, false, 0);
    // In registers, a result has no hidden pointer to pass on.
    check_test_program_forward_shape(test, "struct T{long a,b;};struct T make(int);struct T f(int n){return make(n);}", "forwarded-registers", 0, 0, false, 16);

    // The results, against clang -O0's build: each forwarding shape, a chain of them and a recursive one, through a
    // pointer, a named result's call initializer, a nested argument's call, a big result and a union's; each read back
    // into storage that the run before poisoned, on a stack a call left dirty.
    static const char program[] =
        "__attribute__((noinline)) static void dirty(int n){volatile unsigned char d[16384];for(int i=0;i<16384;i++)d[i]=(unsigned char)(n+i*7+1);}"
        "__attribute__((noinline)) static void poison(void*p,unsigned long size){volatile unsigned char*b=p;for(unsigned long i=0;i<size;i++)b[i]=0xa5;}"
        "struct S{long a[8];};struct B{unsigned char c[4096];};union U{unsigned char c[24];long l[3];};"
        "__attribute__((noinline)) static struct S built(int n){struct S s;for(int i=0;i<8;i++)s.a[i]=n*3+i+1;return s;}"
        "__attribute__((noinline)) static struct S forward(int n){return built(n+1);}"
        "__attribute__((noinline)) static struct S twice(int n){return forward(n*2);}"
        "__attribute__((noinline)) static struct S chain(int n){if(n<=0)return built(n);return chain(n-1);}"
        "__attribute__((noinline)) static struct S choose(int n){if(n&1)return built(n);return twice(n);}"
        "__attribute__((noinline)) static struct S through(struct S(*f)(int),int n){return f(n);}"
        "__attribute__((noinline)) static struct S generic(int n){return _Generic(n,int:(built(n+5)),default:built(0));}"
        "__attribute__((noinline)) static struct S named(int n){struct S s=forward(n);s.a[0]+=100;if(n>3)return s;s.a[7]=-n;return s;}"
        "__attribute__((noinline)) static struct S change(struct S p,int n){p.a[n&7]+=n;return p;}"
        "__attribute__((noinline)) static struct S passed(struct S p,int n){return change(p,n);}"
        "__attribute__((noinline)) static struct S nested(int n){return change(built(n),n+1);}"
        "__attribute__((noinline)) static struct B big(int n){struct B b;for(int i=0;i<4096;i++)b.c[i]=(unsigned char)(n^i^0x5a);return b;}"
        "__attribute__((noinline)) static struct B bigger(int n){return big(n+3);}"
        "__attribute__((noinline)) static struct B named_big(int n){struct B b=bigger(n);b.c[100]^=1;return b;}"
        "__attribute__((noinline)) static union U unite(int n){union U u={.l={n,n+1,n+2}};u.c[0]=(unsigned char)(n+9);return u;}"
        "__attribute__((noinline)) static union U reunite(int n){return unite(n*5);}"
        "__attribute__((noinline)) static void done(int*p){*p+=1;}"
        "__attribute__((noinline)) static struct S kept(int n){if(n>2){__attribute__((cleanup(done)))int guard=n;return built(guard);}return built(-n);}"
        "static long sum(struct S s){long t=0;for(int i=0;i<8;i++)t=t*31+s.a[i];return t;}"
        "int tested(int n){long t=0;"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=forward(n+k);t=t*7+sum(a);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=twice(n+k);t=t*7+sum(a);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=chain(n+k);t=t*7+sum(a);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=choose(n+k);t=t*7+sum(a);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=through(k?twice:forward,n+k);t=t*7+sum(a);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=generic(n+k);t=t*7+sum(a);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=named(n+k*4);t=t*7+sum(a);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S p=built(n);struct S a=passed(p,n+k);t=t*7+sum(a)+sum(p);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=nested(n+k);t=t*7+sum(a);poison(&a,sizeof a);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct B b=bigger(n+k);for(int i=0;i<4096;i+=97)t=t*3+b.c[i];poison(&b,sizeof b);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct B b=named_big(n+k);for(int i=0;i<4096;i+=89)t=t*3+b.c[i];poison(&b,sizeof b);}"
        "for(int k=0;k<2;k++){dirty(n+k);union U u=reunite(n+k);t=t*7+u.l[0]+u.l[1]+u.l[2];poison(&u,sizeof u);}"
        "for(int k=0;k<2;k++){dirty(n+k);struct S a=kept(n+k*3);t=t*7+sum(a);poison(&a,sizeof a);}"
        "struct S w=built(n);w=forward(n+2);t=t*7+sum(w);w=change(w,n);t=t*7+sum(w);"
        "return (int)(t^(t>>32));}";
    check_test_program_source(test, program, "forwarded-results-on-poisoned-storage", clang);
}
