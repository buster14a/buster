// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Differential tests for the resolver's event contract, independent of the parser. The random oracle searches a
// plain list of declarations still in scope. It reads neither production heads nor previous/end columns. A parser
// adapter must additionally test that C source produces the right events; event agreement cannot establish that.

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct CBindingTest CBindingTest;
struct CBindingTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t state;
};

static void c_binding_test_check(CBindingTest* test, bool condition, const char* description)
{
    test->checks += 1;
    test->failures += !condition;
    if (!condition && test->failures <= 12)
    {
        fprintf(stderr, "bindings: %s\n", description);
    }
}

// Original deterministic LCG. Only the upper half is used to avoid the short periods of its low bits.
static uint32_t c_binding_test_random(CBindingTest* test)
{
    test->state = test->state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (uint32_t)(test->state >> 32);
}

static void c_binding_test_compare(CBindingTest* test, const CBindings* bindings, const uint32_t* expected, Arena* scratch, bool require_fallback)
{
    const uint64_t mark = scratch->length;
    uint32_t* scalar = arena_push_array(scratch, (uint64_t)bindings->count + 1, 4, 64);
    uint32_t* batch = scalar != 0 ? arena_push_array(scratch, (uint64_t)bindings->count + 1, 4, 64) : 0;
    CBindingStatistics statistics = { 0 };
    bool valid = batch != 0 && c_bindings_scalar(bindings, scalar, scratch) && c_bindings_batch(bindings, batch, scratch, &statistics);
    c_binding_test_check(test, valid, "resolver allocation/status");
    if (valid)
    {
        for (uint32_t i = 0; i < bindings->count; i += 1)
        {
            c_binding_test_check(test, scalar[i] == batch[i], "batch differs from stack replay");
            c_binding_test_check(test, scalar[i] == expected[i], "resolver differs from independent scope oracle");
        }
        c_binding_test_check(test, !require_fallback || statistics.fallback_uses != 0, "long candidate chain did not use bounded fallback");
        uint64_t* keys = 0;
        uint32_t count = 0;
        valid = c_bindings_sorted(bindings, scratch, &keys, &count, true);
        c_binding_test_check(test, valid, "sort allocation");
        for (uint32_t i = 1; valid && i < count; i += 1)
        {
            c_binding_test_check(test, keys[i - 1] < keys[i], "keys are not in stable name/position order");
        }
    }
    arena_pop_to(scratch, mark);
}

static void c_binding_test_fixtures(CBindingTest* test, CBindings* bindings, Arena* scratch)
{
    c_bindings_reset(bindings);
    uint32_t expected[256] = { 0 };
    c_binding_test_compare(test, bindings, expected, scratch, false);
    // typedef int T; struct T { struct T *next; }; void f(void) { int T[sizeof((T)0)] = ...; }
    const uint32_t outer = c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_TYPEDEF, 10, 11, 0);
    expected[outer - 1] = outer;
    const uint32_t tag = c_bindings_declare(bindings, C_BINDING_TAG, 1, C_BINDING_TAG_KIND, 12, 13, 0);
    expected[tag - 1] = tag;
    expected[c_bindings_use(bindings, C_BINDING_TAG, 1, 14, 14) - 1] = tag;
    const uint32_t scope = c_bindings_mark(bindings);
    expected[c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 20, 20) - 1] = outer;
    const uint32_t inner = c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 18, 22, 1);
    expected[inner - 1] = inner;
    // int x = x sees the new object. (T) * x sees the inner object, not the outer typedef.
    expected[c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 23, 23) - 1] = inner;
    c_binding_test_check(test, bindings->kind[c_bindings_lookup(bindings, C_BINDING_ORDINARY, 1) - 1] == C_BINDING_OBJECT, "object hides typedef");
    c_bindings_restore(bindings, scope, 24);
    expected[c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 25, 25) - 1] = outer;
    // An enumerator named T does not hide T in its own value, but does in the next enumerator's value.
    expected[c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 30, 30) - 1] = outer;
    const uint32_t enumerator = c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_ENUMERATOR, 28, 31, 1);
    expected[enumerator - 1] = enumerator;
    expected[c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 32, 32) - 1] = enumerator;
    c_bindings_restore(bindings, scope, 33);
    // A definition parameter's visibility is disjoint: its prototype and its body, excluding the return declarator.
    const uint32_t parameter = c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 40, 41, 1);
    expected[parameter - 1] = parameter;
    expected[c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 42, 42) - 1] = parameter;
    c_bindings_restore(bindings, scope, 43);
    expected[c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 44, 44) - 1] = outer;
    const uint32_t body_parameter = c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 40, 45, 1);
    expected[body_parameter - 1] = body_parameter;
    expected[c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 46, 46) - 1] = body_parameter;
    // The same spelling in the label namespace can be forward referenced and is independent of the parameter.
    const uint32_t label = c_bindings_declare(bindings, C_BINDING_LABEL, 1, C_BINDING_LABEL_KIND, 52, 46, 1);
    expected[label - 1] = label;
    expected[c_bindings_use(bindings, C_BINDING_LABEL, 1, 47, 47) - 1] = label;
    c_bindings_restore(bindings, scope, 53);
    expected[c_bindings_use(bindings, C_BINDING_LABEL, 1, 54, 54) - 1] = 0;
    c_bindings_restore(bindings, 0, 55);
    c_binding_test_compare(test, bindings, expected, scratch, false);
    c_binding_test_check(test, c_bindings_lookup(bindings, C_BINDING_ORDINARY, 1) == 0 && c_bindings_lookup(bindings, C_BINDING_TAG, 1) == 0, "file scope leaves empty heads");

    // Many closed sibling scopes precede a use of their common name. This must take the bounded fallback.
    c_bindings_reset(bindings);
    memset(expected, 0, sizeof(expected));
    for (uint32_t i = 0; i < 48; i += 1)
    {
        const uint32_t declaration = c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_TYPEDEF, i * 3, i * 3, 1);
        expected[declaration - 1] = declaration;
        c_bindings_restore(bindings, 0, i * 3 + 1);
    }
    c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 145, 145);
    c_binding_test_compare(test, bindings, expected, scratch, true);
}

static void c_binding_test_worlds(CBindingTest* test, CBindings* bindings, Arena* scratch)
{
    uint32_t expected[4096];
    uint32_t replayed[4096];
    uint32_t live[4096];
    uint32_t scopes[128];
    const uint64_t mark = scratch->length;
    CBindingReplay replay;
    const bool ready = c_bindings_replay_initialize(&replay, scratch, bindings->name_capacity, bindings->capacity);
    c_binding_test_check(test, ready, "reusable replay allocation");
    for (uint32_t world = 0; ready && world < 1000; world += 1)
    {
        c_bindings_reset(bindings);
        uint32_t live_count = 0;
        uint32_t depth = 0;
        const uint32_t length = c_binding_test_random(test) % 2000;
        for (uint32_t token = 0; token < length; token += 1)
        {
            const uint32_t action = c_binding_test_random(test) % 12;
            const CBindingNamespace space = (CBindingNamespace)(c_binding_test_random(test) % C_BINDING_NAMESPACES);
            const uint32_t name = 1 + c_binding_test_random(test) % (world % 2 ? 4 : 511);
            if (action == 0 && depth < 128)
            {
                scopes[depth++] = live_count;
                c_binding_test_check(test, c_bindings_mark(bindings) == live_count, "scope mark differs");
            }
            else if (action == 1 && depth != 0)
            {
                live_count = scopes[--depth];
                expected[bindings->count] = 0;
                c_bindings_restore(bindings, live_count, token);
            }
            else if (action < 5)
            {
                const CBindingKind kind = space == C_BINDING_TAG ? C_BINDING_TAG_KIND : space == C_BINDING_LABEL ? C_BINDING_LABEL_KIND : (CBindingKind)(action % 4);
                const uint32_t declaration = c_bindings_declare(bindings, space, name, kind, token, token, depth);
                live[live_count++] = declaration;
                expected[declaration - 1] = declaration;
            }
            else
            {
                uint32_t found = 0;
                const uint32_t key = (uint32_t)space * bindings->name_capacity + name;
                for (uint32_t k = live_count; k != 0 && found == 0; k -= 1)
                {
                    const uint32_t declaration = live[k - 1];
                    if (bindings->key[declaration - 1] == key)
                    {
                        found = declaration;
                    }
                }
                c_binding_test_check(test, c_bindings_lookup(bindings, space, name) == found, "streaming lookup differs");
                const uint32_t use = c_bindings_use(bindings, space, name, token, token);
                expected[use - 1] = found;
            }
        }
        expected[bindings->count] = 0;
        c_bindings_restore(bindings, 0, length);
        c_binding_test_compare(test, bindings, expected, scratch, false);
        c_binding_test_check(test, c_bindings_replay(bindings, &replay, replayed) && memcmp(expected, replayed, (uint64_t)bindings->count * 4) == 0, "reusable replay differs across units");
    }
    arena_pop_to(scratch, mark);
}

static void c_binding_test_deep(CBindingTest* test)
{
    Arena arena = arena_reserve(32ull << 20);
    Arena scratch = arena_reserve(32ull << 20);
    CBindings bindings;
    const uint32_t depth = 100000;
    bool valid = c_bindings_initialize(&bindings, &arena, 4, 3 * depth + 2);
    uint32_t* expected = valid ? arena_push_array(&arena, (uint64_t)bindings.capacity + 1, 4, 64) : 0;
    valid = expected != 0;
    c_binding_test_check(test, valid, "deep test allocation");
    if (valid)
    {
        for (uint32_t i = 0; i < depth; i += 1)
        {
            const uint32_t declaration = c_bindings_declare(&bindings, C_BINDING_ORDINARY, 1, C_BINDING_TYPEDEF, i, i, i);
            expected[declaration - 1] = declaration;
        }
        for (uint32_t i = 0; i < depth; i += 1)
        {
            expected[bindings.count] = 0;
            c_bindings_restore(&bindings, depth - 1 - i, depth + i);
            const uint32_t use = c_bindings_use(&bindings, C_BINDING_ORDINARY, 1, depth + i, depth + i);
            expected[use - 1] = depth - 1 - i;
        }
        c_binding_test_compare(test, &bindings, expected, &scratch, true);
    }
    arena_release(&scratch);
    arena_release(&arena);
}

static void c_binding_test_sort_edges(CBindingTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = mapping != MAP_FAILED && mprotect(mapping + page, (size_t)page, PROT_NONE) == 0;
    c_binding_test_check(test, mapped, "guard page allocation");
    if (mapped)
    {
        uint64_t twin[128];
        uint64_t temporary[128];
        uint64_t twin_temporary[128];
        for (uint32_t length = 0; length <= 96; length += 1)
        {
            uint64_t* keys = (uint64_t*)(mapping + page) - length;
            for (uint32_t shape = 0; shape < 4; shape += 1)
            {
                for (uint32_t i = 0; i < length; i += 1)
                {
                    const uint32_t key = shape == 0 ? 0 : shape == 1 ? UINT32_MAX : shape == 2 ? i % 3 : c_binding_test_random(test);
                    keys[i] = ((uint64_t)key << 32) | i;
                    twin[i] = keys[i];
                }
                c_bindings_sort(keys, temporary, length, true);
                c_bindings_sort(twin, twin_temporary, length, false);
                c_binding_test_check(test, memcmp(keys, twin, (uint64_t)length * 8) == 0, "guarded sort differs from scalar twin");
                for (uint32_t i = 1; i < length; i += 1)
                {
                    c_binding_test_check(test, keys[i - 1] < keys[i], "guarded sort lost stable order");
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (size_t)page * 2);
    }
}

static void c_binding_test_limits(CBindingTest* test, CBindings* bindings, Arena* scratch)
{
    uint32_t result[256];
    CBindingStatistics statistics;
    Arena empty = { 0 };
    CBindings unavailable;
    c_binding_test_check(test, !c_bindings_initialize(&unavailable, &empty, 16, 16), "allocation failure accepted");
    c_binding_test_check(test, !c_bindings_initialize(&unavailable, scratch, UINT32_MAX, 16), "head key overflow accepted");
    c_binding_test_check(test, !c_bindings_initialize(&unavailable, scratch, 16, UINT32_MAX), "activation sentinel overflow accepted");
    c_bindings_reset(bindings);
    c_binding_test_check(test, c_bindings_scalar(bindings, 0, scratch) && c_bindings_batch(bindings, 0, scratch, &statistics), "empty input with no output storage");
    c_binding_test_check(test, !c_bindings_restore(bindings, 1, 0), "invalid scope mark accepted");
    c_bindings_reset(bindings);
    c_binding_test_check(test, c_bindings_declare(bindings, C_BINDING_ORDINARY, 0, C_BINDING_OBJECT, 0, 0, 0) == 0, "zero IdentId accepted");
    c_bindings_reset(bindings);
    c_binding_test_check(test, c_bindings_use(bindings, C_BINDING_ORDINARY, bindings->name_capacity, 0, 0) == 0, "out of bounds IdentId accepted");
    c_bindings_reset(bindings);
    c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 0, 5, 0);
    c_binding_test_check(test, c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 1, 4) == 0, "backward point accepted");
    c_bindings_reset(bindings);
    const uint32_t saved_capacity = bindings->capacity;
    bindings->capacity = 1;
    c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 0, 0, 0);
    c_binding_test_check(test, c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 1, 1) == 0, "event exhaustion accepted");
    c_binding_test_check(test, !c_bindings_scalar(bindings, result, scratch) && !c_bindings_batch(bindings, result, scratch, &statistics), "partial unit resolved after failure");
    bindings->capacity = saved_capacity;
    c_bindings_reset(bindings);
    c_binding_test_check(test, c_bindings_lookup(bindings, C_BINDING_ORDINARY, 1) == 0, "reset failed to undo exhausted unit");
    c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, 0, 0, 0);
    c_binding_test_check(test, !c_bindings_scalar(bindings, result, &empty) && !c_bindings_batch(bindings, result, &empty, &statistics), "scratch exhaustion accepted");
    c_binding_test_check(test, empty.length == 0, "failed resolver leaked scratch");

    // Refuse failures at each intermediate array too, not only a wholly empty arena. A failed constructor rolls
    // back its allocations; a failed resolver always rolls back scratch, including its lazy scalar fallback.
    for (uint64_t bytes = 0; bytes < 2048; bytes += 17)
    {
        Arena limited = *scratch;
        limited.capacity = limited.length + bytes;
        CBindings partial;
        const uint64_t mark = limited.length;
        const bool initialized = c_bindings_initialize(&partial, &limited, 4, 16);
        c_binding_test_check(test, initialized || (partial.heads == 0 && limited.length == mark), "failed constructor did not roll back");
    }
    c_bindings_reset(bindings);
    for (uint32_t i = 0; i < 48; i += 1)
    {
        c_bindings_declare(bindings, C_BINDING_ORDINARY, 1, C_BINDING_OBJECT, i, i, 1);
        c_bindings_restore(bindings, 0, i);
    }
    c_bindings_use(bindings, C_BINDING_ORDINARY, 1, 49, 49);
    uint32_t expected[256];
    c_binding_test_check(test, c_bindings_scalar(bindings, expected, scratch), "failure-test oracle");
    for (uint64_t bytes = 0; bytes < 32768; bytes += 97)
    {
        Arena limited = *scratch;
        limited.capacity = limited.length + bytes;
        const uint64_t mark = limited.length;
        const bool resolved = c_bindings_batch(bindings, result, &limited, &statistics);
        c_binding_test_check(test, limited.length == mark, "failed batch leaked scratch");
        c_binding_test_check(test, !resolved || memcmp(expected, result, (uint64_t)bindings->count * 4) == 0, "limited-memory fallback returned a wrong result");
    }
}

static int run_c_bindings_tests(int argc, char** argv)
{
    CBindingTest test = { .state = 1 };
    bool arguments = argc == 0 || (argc == 2 && strcmp(argv[0], "--seed") == 0);
    if (arguments && argc == 2)
    {
        char* end = 0;
        test.state = strtoull(argv[1], &end, 0);
        arguments = end != argv[1] && *end == 0;
    }
    if (!arguments)
    {
        fprintf(stderr, "usage: test-bindings [--seed N]\n");
    }
    Arena arena = arena_reserve(4ull << 20);
    Arena scratch = arena_reserve(16ull << 20);
    CBindings bindings;
    const bool initialized = arguments && c_bindings_initialize(&bindings, &arena, 1024, 4096);
    c_binding_test_check(&test, initialized, "test setup");
    if (initialized)
    {
        c_binding_test_fixtures(&test, &bindings, &scratch);
        c_binding_test_worlds(&test, &bindings, &scratch);
        c_binding_test_sort_edges(&test);
        c_binding_test_deep(&test);
        c_binding_test_limits(&test, &bindings, &scratch);
    }
    arena_release(&scratch);
    arena_release(&arena);
    printf("bindings: %llu checks, %llu failures\n", (unsigned long long)test.checks, (unsigned long long)test.failures);
    return test.failures != 0;
}
