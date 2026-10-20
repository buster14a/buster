// The call graph check's tests, for the test-call-graph command:
//
//   build/compiler test-call-graph
//
// main.c includes this file after call_graph.c and calls run_call_graph_tests for the command. Each case is the IR of
// one or more translation units and an allowlist, and call_graph_check's report must be exactly the expected one, with
// the expected success:
//   - shapes: no cycle among the call forms clang writes (a variadic callee's type, a struct return type, a quoted name,
//     tail calls and invoke), with what isn't a call of a defined function: a call through a pointer, a declared
//     function, an intrinsic, inline assembly whose template looks like a call, and functions passed as arguments or
//     stored;
//   - a self-call, a two-function cycle with a caller outside it, and a cycle with a chord whose path must be the
//     shortest one;
//   - a cycle through three translation units' external functions, beside static functions of the same name in two of
//     them and an external one in the third: a callee resolves to its own unit's static function first;
//   - allowed cycles: two units' copies of the same static pair, both allowed by one entry; then a cycle that grew past
//     its entry, an allowed self-call and an entry without a cycle;
//   - malformed IR: definitions without a closing brace or a name;
//   - depth: a chain of BUSTER_CALL_GRAPH_TEST_CHAIN functions that ends in a self-call, and a ring of
//     BUSTER_CALL_GRAPH_TEST_RING functions, which would overflow a recursive walk's stack.
//
// The exit status is 1 if a case fails.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define BUSTER_CALL_GRAPH_TEST_CHAIN 1000000
#define BUSTER_CALL_GRAPH_TEST_RING 100000

typedef struct CallGraphTestCase CallGraphTestCase;
struct CallGraphTestCase
{
    const char* name;
    const CallGraphInput* inputs;
    uint32_t input_count;
    const CallGraphAllowed* allowed;
    uint64_t allowed_count;
    String8 expected;
    bool success;
};

static const CallGraphInput call_graph_test_shapes[] = {
    {
        .name = BUSTER_S8_INIT("shapes.ll"),
        .text = BUSTER_S8_INIT("; ModuleID = 'shapes.c'\n"
                               "%struct.Range64 = type { i64, i64 }\n"
                               "@.str = private unnamed_addr constant [3 x i8] c\"%d\\00\", align 1\n"
                               "\n"
                               "define dso_local i32 @main(i32 noundef %0, ptr noundef %1) #0 {\n"
                               "  %3 = alloca ptr, align 8\n"
                               "  store ptr @helper, ptr %3, align 8\n"
                               "  %4 = load ptr, ptr %3, align 8\n"
                               "  call void %4()\n"
                               "  %5 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef 1)\n"
                               "  %6 = call %struct.Range64 @range(i64 noundef 2)\n"
                               "  tail call void @\"quoted.name\"(ptr noundef @main)\n"
                               "  call void asm sideeffect \"call @main(\", \"~{dirflag}\"()\n"
                               "  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %3, ptr align 8 %3, i64 8, i1 false)\n"
                               "  ret i32 0\n"
                               "}\n"
                               "\n"
                               "define internal %struct.Range64 @range(i64 noundef %0) #0 {\n"
                               "  %2 = musttail call zeroext i1 @helper()\n"
                               "  ret %struct.Range64 zeroinitializer\n"
                               "}\n"
                               "\n"
                               "define internal zeroext i1 @helper() #0 {\n"
                               "  ret i1 true\n"
                               "}\n"
                               "\n"
                               "define internal void @\"quoted.name\"(ptr noundef %0) #0 {\n"
                               "  %2 = invoke zeroext i1 @helper()\n"
                               "          to label %3 unwind label %4\n"
                               "  ret void\n"
                               "}\n"
                               "\n"
                               "declare i32 @printf(ptr noundef, ...) #1\n"
                               "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n"),
    },
};

static const CallGraphInput call_graph_test_self[] = {
    {
        .name = BUSTER_S8_INIT("self.ll"),
        .text = BUSTER_S8_INIT("define internal void @a(i32 noundef %0) #0 {\n"
                               "  call void @a(i32 noundef 1)\n"
                               "  ret void\n"
                               "}\n"
                               "define dso_local void @b() #0 {\n"
                               "  call void @a(i32 noundef 0)\n"
                               "  ret void\n"
                               "}\n"),
    },
};

static const CallGraphInput call_graph_test_pair[] = {
    {
        .name = BUSTER_S8_INIT("pair.ll"),
        .text = BUSTER_S8_INIT("define dso_local void @d() {\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @a() {\n"
                               "  call void @b()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @b() {\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"),
    },
};

static const CallGraphInput call_graph_test_chord[] = {
    {
        .name = BUSTER_S8_INIT("chord.ll"),
        .text = BUSTER_S8_INIT("define internal void @a() {\n"
                               "  call void @b()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @b() {\n"
                               "  call void @c()\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @c() {\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"),
    },
};

static const CallGraphInput call_graph_test_units[] = {
    {
        .name = BUSTER_S8_INIT("one.ll"),
        .text = BUSTER_S8_INIT("define dso_local void @x() {\n"
                               "  call void @y()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @h() {\n"
                               "  call void @y()\n"
                               "  ret void\n"
                               "}\n"),
    },
    {
        .name = BUSTER_S8_INIT("two.ll"),
        .text = BUSTER_S8_INIT("define dso_local void @y() {\n"
                               "  call void @h()\n"
                               "  call void @z()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @h() {\n"
                               "  ret void\n"
                               "}\n"),
    },
    {
        .name = BUSTER_S8_INIT("three.ll"),
        .text = BUSTER_S8_INIT("define dso_local void @z() {\n"
                               "  call void @x()\n"
                               "  ret void\n"
                               "}\n"
                               "define dso_local void @h() {\n"
                               "  call void @z()\n"
                               "  ret void\n"
                               "}\n"),
    },
};

static const CallGraphInput call_graph_test_copies[] = {
    {
        .name = BUSTER_S8_INIT("first.ll"),
        .text = BUSTER_S8_INIT("define internal void @a() {\n"
                               "  call void @b()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @b() {\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"),
    },
    {
        .name = BUSTER_S8_INIT("second.ll"),
        .text = BUSTER_S8_INIT("define internal void @a() {\n"
                               "  call void @b()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @b() {\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"),
    },
};

static const CallGraphAllowed call_graph_test_copies_allowed[] = {
    {
        .functions = BUSTER_S8_INIT("b a"),
    },
};

static const CallGraphInput call_graph_test_grown[] = {
    {
        .name = BUSTER_S8_INIT("grown.ll"),
        .text = BUSTER_S8_INIT("define internal void @a() {\n"
                               "  call void @b()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @b() {\n"
                               "  call void @c()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @c() {\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @f() {\n"
                               "  call void @f()\n"
                               "  ret void\n"
                               "}\n"),
    },
};

static const CallGraphAllowed call_graph_test_grown_allowed[] = {
    {
        .functions = BUSTER_S8_INIT("a b"),
    },
    {
        .functions = BUSTER_S8_INIT("f"),
    },
    {
        .functions = BUSTER_S8_INIT("e"),
    },
};

static const CallGraphInput call_graph_test_malformed[] = {
    {
        .name = BUSTER_S8_INIT("broken.ll"),
        .text = BUSTER_S8_INIT("define internal void @a() {\n"
                               "  ret void\n"
                               "define void @(i32 %0) {\n"
                               "}\n"
                               "define internal void @b() {\n"
                               "  call void @b()\n"),
    },
};

static const CallGraphTestCase call_graph_test_cases[] = {
    {
        .name = "shapes",
        .inputs = call_graph_test_shapes,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_shapes),
        .expected = BUSTER_S8_INIT("call graph: 1 translation unit, 4 functions, 4 calls between them, 0 cycles, 0 allowed\n"),
        .success = true,
    },
    {
        .name = "self-call",
        .inputs = call_graph_test_self,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_self),
        .expected = BUSTER_S8_INIT("cycle: self.ll:a -> self.ll:a\n"
                                   "call graph: 1 translation unit, 2 functions, 2 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "two-function cycle",
        .inputs = call_graph_test_pair,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_pair),
        .expected = BUSTER_S8_INIT("cycle: pair.ll:a -> pair.ll:b -> pair.ll:a\n"
                                   "call graph: 1 translation unit, 3 functions, 3 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "shortest path",
        .inputs = call_graph_test_chord,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_chord),
        .expected = BUSTER_S8_INIT("cycle: chord.ll:a -> chord.ll:b -> chord.ll:a\n"
                                   "call graph: 1 translation unit, 3 functions, 4 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "three translation units",
        .inputs = call_graph_test_units,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_units),
        .expected = BUSTER_S8_INIT("cycle: one.ll:x -> two.ll:y -> three.ll:z -> one.ll:x\n"
                                   "call graph: 3 translation units, 6 functions, 6 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "allowed copies",
        .inputs = call_graph_test_copies,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_copies),
        .allowed = call_graph_test_copies_allowed,
        .allowed_count = BUSTER_ARRAY_LENGTH(call_graph_test_copies_allowed),
        .expected = BUSTER_S8_INIT("allowed cycle: first.ll:a -> first.ll:b -> first.ll:a\n"
                                   "allowed cycle: second.ll:a -> second.ll:b -> second.ll:a\n"
                                   "call graph: 2 translation units, 4 functions, 4 calls between them, 0 cycles, 2 allowed\n"),
        .success = true,
    },
    {
        .name = "grown cycle",
        .inputs = call_graph_test_grown,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_grown),
        .allowed = call_graph_test_grown_allowed,
        .allowed_count = BUSTER_ARRAY_LENGTH(call_graph_test_grown_allowed),
        .expected = BUSTER_S8_INIT("cycle: grown.ll:a -> grown.ll:b -> grown.ll:c -> grown.ll:a\n"
                                   "allowed cycle: grown.ll:f -> grown.ll:f\n"
                                   "allowlist entry without a cycle: a b\n"
                                   "allowlist entry without a cycle: e\n"
                                   "call graph: 1 translation unit, 4 functions, 4 calls between them, 1 cycle, 1 allowed\n"),
        .success = false,
    },
    {
        .name = "malformed",
        .inputs = call_graph_test_malformed,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_malformed),
        .expected = BUSTER_S8_INIT("broken.ll:1: a definition without its closing brace\n"
                                   "broken.ll:3: a definition without a name\n"
                                   "broken.ll:5: a definition without its closing brace\n"
                                   "cycle: broken.ll:b -> broken.ll:b\n"
                                   "call graph: 1 translation unit, 2 functions, 1 call between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
};

typedef struct CallGraphTestText CallGraphTestText;
struct CallGraphTestText
{
    Arena* arena;
    char* begin;
    uint64_t length;
    bool full;
};

// Appends to text pushed onto the end of the arena, as the report is.
static void call_graph_test_printf(CallGraphTestText* text, const char* format, uint64_t a, uint64_t b)
{
    char line[160];
    const int length = snprintf(line, sizeof(line), format, (unsigned long long)a, (unsigned long long)b);
    char* bytes = arena_push(text->arena, (uint64_t)length, 1);
    if (bytes == 0)
    {
        text->full = true;
    }
    else
    {
        memcpy(bytes, line, (uint64_t)length);
        text->length += (uint64_t)length;
    }
}

static CallGraphTestText call_graph_test_text(Arena* arena)
{
    CallGraphTestText result = {
        .arena = arena,
        .begin = (char*)arena->base + arena->length,
        .length = 0,
        .full = false,
    };
    return result;
}

static bool call_graph_test_run(Arena* arena, const CallGraphTestCase* test)
{
    const uint64_t length = arena->length;
    const CallGraphResult check = call_graph_check(arena, test->inputs, test->input_count, test->allowed, test->allowed_count);
    const bool result = !check.full && check.success == test->success && call_graph_equal(check.report, test->expected);
    if (result)
    {
        printf("  %s: ok\n", test->name);
    }
    else
    {
        printf("  %s: FAILED (success %d, expected %d%s)\n", test->name, check.success, test->success, check.full ? ", out of memory" : "");
        if (check.report.length < 4096 && test->expected.length < 4096)
        {
            printf("    expected:\n%.*s    got:\n%.*s", (int)test->expected.length, test->expected.pointer, (int)check.report.length, check.report.pointer);
        }
    }
    arena_pop_to(arena, length);
    return result;
}

static int run_call_graph_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    Arena arena = arena_reserve(BUSTER_CALL_GRAPH_ARENA_CAPACITY);
    if (argc != 0)
    {
        fprintf(stderr, "usage: test-call-graph\n");
    }
    else
    {
        uint64_t failures = 0;
        uint64_t cases = 0;
        printf("test-call-graph:\n");
        for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(call_graph_test_cases); i += 1)
        {
            failures += !call_graph_test_run(&arena, &call_graph_test_cases[i]);
            cases += 1;
        }

        // The chain: f0 calls f1, and so on, and the last calls itself.
        CallGraphTestText chain = call_graph_test_text(&arena);
        for (uint64_t f = 0; f < BUSTER_CALL_GRAPH_TEST_CHAIN; f += 1)
        {
            call_graph_test_printf(&chain, "define internal void @f%llu() {\n  call void @f%llu()\n  ret void\n}\n", f, f + 1 < BUSTER_CALL_GRAPH_TEST_CHAIN ? f + 1 : f);
        }
        CallGraphTestText chain_expected = call_graph_test_text(&arena);
        call_graph_test_printf(&chain_expected, "cycle: deep.ll:f%llu -> deep.ll:f%llu\n", BUSTER_CALL_GRAPH_TEST_CHAIN - 1, BUSTER_CALL_GRAPH_TEST_CHAIN - 1);
        call_graph_test_printf(&chain_expected, "call graph: 1 translation unit, %llu functions, %llu calls between them, 1 cycle, 0 allowed\n", BUSTER_CALL_GRAPH_TEST_CHAIN, BUSTER_CALL_GRAPH_TEST_CHAIN);
        const CallGraphInput chain_input = {
            .name = BUSTER_S8_INIT("deep.ll"),
            .text = { .pointer = chain.begin, .length = chain.length },
        };

        // The ring: the same, but the last calls f0, so the cycle's path names every function.
        CallGraphTestText ring = call_graph_test_text(&arena);
        for (uint64_t f = 0; f < BUSTER_CALL_GRAPH_TEST_RING; f += 1)
        {
            call_graph_test_printf(&ring, "define internal void @f%llu() {\n  call void @f%llu()\n  ret void\n}\n", f, (f + 1) % BUSTER_CALL_GRAPH_TEST_RING);
        }
        CallGraphTestText ring_expected = call_graph_test_text(&arena);
        call_graph_test_printf(&ring_expected, "cycle: ", 0, 0);
        for (uint64_t f = 0; f <= BUSTER_CALL_GRAPH_TEST_RING; f += 1)
        {
            call_graph_test_printf(&ring_expected, f < BUSTER_CALL_GRAPH_TEST_RING ? "ring.ll:f%llu -> " : "ring.ll:f%llu\n", f % BUSTER_CALL_GRAPH_TEST_RING, 0);
        }
        call_graph_test_printf(&ring_expected, "call graph: 1 translation unit, %llu functions, %llu calls between them, 1 cycle, 0 allowed\n", BUSTER_CALL_GRAPH_TEST_RING, BUSTER_CALL_GRAPH_TEST_RING);
        const CallGraphInput ring_input = {
            .name = BUSTER_S8_INIT("ring.ll"),
            .text = { .pointer = ring.begin, .length = ring.length },
        };

        const CallGraphTestCase depth_cases[] = {
            {
                .name = "deep chain",
                .inputs = &chain_input,
                .input_count = 1,
                .expected = { .pointer = chain_expected.begin, .length = chain_expected.length },
                .success = false,
            },
            {
                .name = "long ring",
                .inputs = &ring_input,
                .input_count = 1,
                .expected = { .pointer = ring_expected.begin, .length = ring_expected.length },
                .success = false,
            },
        };
        const bool generated = !chain.full && !chain_expected.full && !ring.full && !ring_expected.full;
        for (uint64_t i = 0; generated && i < BUSTER_ARRAY_LENGTH(depth_cases); i += 1)
        {
            failures += !call_graph_test_run(&arena, &depth_cases[i]);
            cases += 1;
        }
        if (!generated)
        {
            printf("  depth: FAILED (out of memory)\n");
            failures += 1;
        }

        printf("test-call-graph: %llu cases, %llu failed\n", (unsigned long long)cases, (unsigned long long)failures);
        result = failures == 0 ? 0 : 1;
    }
    arena_release(&arena);
    return result;
}
