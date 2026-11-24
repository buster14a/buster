// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1375: a function that returns a struct or union through its hidden pointer, every return naming the same automatic
// object, builds that object where the hidden pointer points, as clang's NRVO does at -O0: no copy at its returns, no
// frame bytes of its own. The excluded shapes keep the object and its copy. Include after c_check_program_clear_test.c,
// whose shape helper (check_test_program_call_initializer_shape) counts a unit's inline copies, memmove and object
// bytes by both paths. Clang is the result oracle.

static void check_test_program_named_results(CheckTest* test, const char* clang)
{
    // Both sides of the inline copy's limit (BUSTER_TIER0_MAX_COPY_BYTES, past which a copy is memmove's), each return
    // of the object building nothing more.
    static const uint32_t sizes[] = { 17, 24, 64, 256, 257, 4096 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        char source[512];
        char name[96];
        snprintf(source, sizeof(source), "struct S{unsigned char c[%u];};void fill(struct S*,int);"
                                         "struct S f(int n){struct S s;fill(&s,n);if(n>3)return s;s.c[0]=(unsigned char)n;return (s);}", sizes[i]);
        snprintf(name, sizeof(name), "named-result-%u", sizes[i]);
        check_test_program_call_initializer_shape(test, source, name, 0, false, 0);
    }
    check_test_program_call_initializer_shape(test, "union U{unsigned char c[64];long l;};void fill(union U*,int);"
                                                    "union U f(int n){union U u;fill(&u,n);return u;}",
                                              "named-result-union", 0, false, 0);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct S f(int n){const struct S s={.a[3]=n};return s;}",
                                              "named-result-const", 0, false, 0);
    // A compound literal is built in place too, where its return evaluates it.
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct S f(int n){return (struct S){.a[3]=n,.a[7]=n+1};}",
                                              "named-result-compound-literal", 0, false, 0);
    // Its address may escape: the object is the caller's, whose storage nothing else reaches during the call.
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct S*leak;void fill(struct S*,int);"
                                                    "struct S f(int n){struct S s;leak=&s;fill(&s,n);return s;}",
                                              "named-result-escaping-address", 0, false, 0);
    // A return in a statement expression names the same object.
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(struct S*,int);"
                                                    "struct S f(int n){struct S s;fill(&s,n);if(n)({if(n>1)return s;});return s;}",
                                              "named-result-statement-expression", 0, false, 0);
    // A cleanup runs before a named return value returns, which clang's Sema decides the same way: in place, it writes the
    // returned object, as clang's code does.
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(struct S*,int);void done(int*);"
                                                    "struct S f(int n){__attribute__((cleanup(done)))int guard=n;struct S s;fill(&s,n);return s;}",
                                              "named-result-cleanup", 0, false, 0);
    // The object's own call initializer passes the hidden pointer on (#2684, c_check_program_forward_test.c): no copy, no
    // object of the call's.
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){struct S s=make(n);s.a[0]+=1;return s;}",
                                              "named-result-call-initializer", 0, false, 0);

    // Every other shape keeps its object and its copy.
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(struct S*,int);"
                                                    "struct S f(int n){struct S a,b;fill(&a,n);fill(&b,n+1);if(n)return a;return b;}",
                                              "named-result-two-objects", 2, false, 128);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(struct S*,int);"
                                                    "struct S f(int n){struct S s;fill(&s,n);if(n)return (struct S){.a[1]=n};return s;}",
                                              "named-result-object-and-literal", 2, false, 128);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct S f(struct S p){p.a[0]+=1;return p;}",
                                              "named-result-parameter", 1, false, 64);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct S g;struct S f(void){return g;}",
                                              "named-result-global", 1, false, 0);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct S f(int n){static struct S s;s.a[0]=n;return s;}",
                                              "named-result-static", 1, false, 0);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct S f(const struct S*p){return *p;}",
                                              "named-result-dereference", 1, false, 0);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};struct W{struct S s;long k;};void fill(struct W*,int);"
                                                    "struct S f(int n){struct W w;fill(&w,n);return w.s;}",
                                              "named-result-member", 1, false, 72);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(volatile struct S*,int);"
                                                    "struct S f(int n){volatile struct S s;fill(&s,n);return s;}",
                                              "named-result-volatile", 1, false, 64);
    check_test_program_call_initializer_shape(test, "struct S{long a[7];volatile long v;};void fill(struct S*,int);"
                                                    "struct S f(int n){struct S s;fill(&s,n);return s;}",
                                              "named-result-volatile-member", 1, false, 64);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(struct S*,int);"
                                                    "struct S f(int n){_Alignas(64) struct S s;fill(&s,n);return s;}",
                                              "named-result-overaligned", 1, false, 64);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};typedef struct S A __attribute__((aligned(32)));void fill(A*,int);"
                                                    "A f(int n){A s;fill(&s,n);return s;}",
                                              "named-result-aligned-typedef", 1, false, 64);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(struct S*,int);void*b[5];"
                                                    "struct S f(int n){int r=__builtin_setjmp(b);struct S s;fill(&s,n+r);return s;}",
                                              "named-result-setjmp", 1, false, 64);
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(struct S*,int);int resume(void)__attribute__((returns_twice));"
                                                    "struct S f(void){struct S s;fill(&s,resume());return s;}",
                                              "named-result-returns-twice", 1, false, 64);
    // A cleanup that comes after a value the return keeps: an over-aligned object isn't a named return value, whose
    // bytes the return copies where the hidden pointer points before its cleanups.
    check_test_program_call_initializer_shape(test, "struct S{long a[8];};void fill(struct S*,int);void done(int*);"
                                                    "struct S f(int n){__attribute__((cleanup(done)))int guard=n;_Alignas(64) struct S s;fill(&s,n);return s;}",
                                              "named-result-kept-before-cleanup", 1, false, 64);
    // In registers, a result has no hidden pointer: its bytes' address returns as before.
    check_test_program_call_initializer_shape(test, "struct T{long a,b;};void fill(struct T*,int);struct T f(int n){struct T t;fill(&t,n);return t;}",
                                              "named-result-registers", 0, false, 16);

    // The results, against clang -O0's build, on a stack a call left dirty: each shape above, its object's escaping
    // address read back after the call, a recursive chain, a named result that the next call's argument names, and a
    // cleanup that writes the returned object.

    check_test_program_suite(test, "checker/programs/program-named-results", clang);
}
