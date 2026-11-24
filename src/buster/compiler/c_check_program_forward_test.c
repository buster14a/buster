// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2684: a call whose result is the function's own, through the hidden pointer, passes that pointer on, as clang -O0's
// code passes its sret pointer on: `return f(...);` and the named result's (#1375) call initializer. Its callee writes
// the result where the function's caller wants it: no inline copy, no memmove and no object for the result, by both
// paths. The excluded shapes keep their object and their copy. Include after c_check_program_named_result_test.c (and
// c_check_program_clear_test.c, whose shape helper counts a unit's forwarded calls, inline copies, memmove and object
// bytes). Clang is the result oracle.

// A unit's shape by both paths: its forwarded calls (BUSTER_TIER0_RESULT_HIDDEN), inline copies, calls of memmove and
// largest function's object bytes, and whether a forwarded result is past the inline copy's limit, whose function then
// gives Tier 1's copy of it memmove (Tier0Function.hidden_copy), which Tier 0 never calls; one without a copy leaves no
// address to discard either.
static void check_test_program_forward_shape(CheckTest* test, const char* source, const char* name, uint32_t forwarded, uint32_t copies, bool moves,
                                             uint64_t bytes, bool big)
{
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CheckTestClearShape shape = { 0 };
        char description[384];
        const bool lowered = check_test_program_clear_lower(source, path == 0, &shape);
        snprintf(description, sizeof(description),
                 "%s: %s path forwards %u calls and emits %u inline copies, memmove %u, %" PRIu64 " object bytes and %u hidden copies (got %u, %u, %u, %" PRIu64
                 ", %u, %u discarded addresses)",
                 name, path == 0 ? "scalar" : "vector", forwarded, copies, moves, bytes, big, shape.forwarded, shape.copies, shape.move_calls, shape.object_bytes,
                 shape.hidden_copies, shape.dead_addresses);
        check_test_expect(test, lowered && shape.forwarded == forwarded && shape.copies == copies && (shape.move_calls != 0) == moves && shape.object_bytes == bytes &&
                                    shape.hidden_copies == (big ? 1u : 0u) && (copies != 0 || moves || shape.dead_addresses == 0),
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
        check_test_program_forward_shape(test, source, name, 1, 0, false, 0, sizes[i] > BUSTER_TIER0_MAX_COPY_BYTES);
        snprintf(source, sizeof(source), "struct S{unsigned char c[%u];};struct S make(int);struct S f(int n){struct S s=make(n);s.c[0]+=1;return s;}", sizes[i]);
        snprintf(name, sizeof(name), "forwarded-named-result-%u", sizes[i]);
        check_test_program_forward_shape(test, source, name, 1, 0, false, 0, sizes[i] > BUSTER_TIER0_MAX_COPY_BYTES);
    }
    check_test_program_forward_shape(test, "union U{unsigned char c[64];long l;};union U make(int);union U f(int n){return make(n);}", "forwarded-union", 1, 0, false, 0, false);
    // Through the conversions and the selections around the call, and through a pointer.
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return (make(n));}", "forwarded-parenthesized", 1, 0, false, 0, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return _Generic(n,int:make(n),default:make(0));}",
                                     "forwarded-generic", 1, 0, false, 0, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return __builtin_choose_expr(1,make(n),make(0));}",
                                     "forwarded-choose", 1, 0, false, 0, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return __extension__ make(n);}", "forwarded-extension", 1, 0, false,
                                     0, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};const struct S make(int);struct S f(int n){return make(n);}", "forwarded-qualified-result", 1, 0,
                                     false, 0, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S f(struct S(*make)(int),int n){return make(n);}", "forwarded-indirect", 1, 0, false, 0, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};typedef struct S A __attribute__((aligned(32)));A make(int);struct S f(int n){return make(n);}",
                                     "forwarded-aligned-typedef", 1, 0, false, 0, false);
    // Every return of a call forwards; an argument's call keeps its own object, as #1549's nested calls do.
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S other(int);struct S f(int n){if(n)return make(n);return other(n);}",
                                     "forwarded-two-returns", 2, 0, false, 0, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S change(struct S,int);struct S f(int n){return change(make(n),n);}",
                                     "forwarded-nested-argument", 1, 0, false, 64, false);
    // A return of another object keeps its copy beside a forwarded one, as no object is the named result.
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);void fill(struct S*,int);"
                                           "struct S f(int n){struct S s;fill(&s,n);if(n)return s;return make(n);}",
                                     "forwarded-beside-a-copy", 1, 1, false, 64, false);
    // The named result's address may escape before its initializer's call runs only through the initializer, which
    // keeps the call's own object then (check_lower_init_names_destination).
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(struct S*,int);struct S f(int n){struct S s=make(&s,n);return s;}",
                                     "forwarded-named-result-self-reference", 0, 1, false, 64, false);

    // Every other shape keeps its object and its copy, or has neither.
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);void done(int*);"
                                           "struct S f(int n){__attribute__((cleanup(done)))int guard=n;return make(n);}",
                                     "forwarded-kept-before-cleanup", 0, 1, false, 64, false);
    // The same with 0 to 20 expression statements (one EVALUATE control each) before the kept return, so that its RESULT
    // takes every lane of check_lower_forwards' 16 controls at a time: no call of the function forwards, the last
    // return's call included.
    for (uint32_t k = 0; k <= 20; k += 1)
    {
        char source[1024];
        int length = snprintf(source, sizeof(source), "struct S{long a[8];};struct S make(long);void done(long*);struct S f(long n){");
        for (uint32_t e = 0; length > 0 && (size_t)length < sizeof(source) && e < k; e += 1)
        {
            length += snprintf(source + length, sizeof(source) - (size_t)length, "n+=%u;", e + 1);
        }
        length += snprintf(source + length, sizeof(source) - (size_t)length,
                           "{__attribute__((cleanup(done)))long c=n;if(n&1)return make(c+1);}return make(n+5);}");
        CheckTestClearShape shape = { 0 };
        const bool fits = length > 0 && (size_t)length < sizeof(source);
        const bool lowered = fits && check_test_program_clear_lower(source, false, &shape);
        char description[160];
        snprintf(description, sizeof(description), "a kept return's RESULT after %u statements keeps the function's calls from forwarding (%u forwarded)", k,
                 shape.forwarded);
        check_test_expect(test, lowered && shape.forwarded == 0, description);
    }
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);void done(int*);"
                                           "struct S f(int n){if(n>3){__attribute__((cleanup(done)))int guard=n;return make(guard);}return make(n);}",
                                     "forwarded-body-with-a-kept-return", 0, 2, false, 128, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);void*b[5];struct S f(int n){int r=__builtin_setjmp(b);return make(n+r);}",
                                     "forwarded-setjmp", 0, 1, false, 64, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int)__attribute__((returns_twice));struct S f(int n){return make(n);}",
                                     "forwarded-returns-twice", 0, 1, false, 64, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return n?make(n):make(-n);}", "forwarded-conditional", 0, 1,
                                     false, 128, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){return (n++,make(n));}", "forwarded-comma", 0, 1, false, 64, false);
    check_test_program_forward_shape(test, "struct S{long a[8];};struct S make(int);struct S f(int n){__attribute__((musttail))return make(n);}", "forwarded-musttail",
                                     0, 0, false, 0, false);
    // In registers, a result has no hidden pointer to pass on.
    check_test_program_forward_shape(test, "struct T{long a,b;};struct T make(int);struct T f(int n){return make(n);}", "forwarded-registers", 0, 0, false, 16, false);

    // The results, against clang -O0's build: each forwarding shape, a chain of them and a recursive one, through a
    // pointer, a named result's call initializer, a nested argument's call, a big result and a union's; each read back
    // into storage that the run before poisoned, on a stack a call left dirty.

    check_test_program_suite(test, "checker/programs/program-forwarded-results", clang);
}
