// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
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
//   - aliases: clang's IR of a function that calls itself through an alias and through an internal alias of an alias,
//     and of a call of an ifunc (a problem); then hand-written aliases that lead to no definition: round a loop, and to
//     a declaration;
//   - binding: clang's IR of a weak function beside a strong alias of the same name that leads to recursion (the alias
//     wins), and of a strong function beside a weak alias in the caller's unit (the function wins, but the unit's own
//     alias counts too), each in both orders; two strong definitions (a problem, and both count); and two weak ones
//     (either may be linked, so both count); then, in both orders, an alias of a weak function that another unit
//     replaces (the alias keeps its own module's body, which calls the alias), and weak or available_externally bodies
//     that call back into their unit beside a strong definition in another, which the unit's calls may run inlined:
//     available_externally, always_inline, called from a flatten function, and always_inline beside a section name
//     that looks like an attribute group; then a replaced weak body, which its own unit's call reaches and another
//     unit's doesn't;
//   - allowed cycles: two units' copies of the same static pair, both allowed by one entry; then a cycle that grew past
//     its entry, an allowed self-call and an entry without a cycle; then a cycle through two units' static functions of
//     the same name, which an entry naming the other functions once each, and another, mustn't match;
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

// clang 23's IR of this, at -O0 without LLVM's passes, without its metadata and attributes (-O3, as build.sh captures,
// differs only in attributes, metadata and lifetime intrinsics):
//
//   int again(int) __attribute__((alias("recurse")));
//   static int hidden_again(int) __attribute__((alias("chain")));
//   int chain(int) __attribute__((alias("again")));
//   int recurse(int n)
//   {
//       int result = 0;
//       if (n != 0)
//       {
//           result = again(n - 1) + hidden_again(n - 1) + n;
//       }
//       return result;
//   }
//   static int pick(void) { return 0; }
//   static int (*resolve(void))(void) { return pick; }
//   int dispatched(void) __attribute__((ifunc("resolve")));
//   int use(void) { return dispatched(); }
static const CallGraphInput call_graph_test_alias[] = {
    {
        .name = BUSTER_S8_INIT("alias.ll"),
        .text = BUSTER_S8_INIT("@again = dso_local alias i32 (i32), ptr @recurse\n"
                               "@hidden_again = internal alias i32 (i32), ptr @chain\n"
                               "@chain = dso_local alias i32 (i32), ptr @again\n"
                               "@dispatched = dso_local ifunc i32 (), ptr @resolve\n"
                               "\n"
                               "define dso_local i32 @recurse(i32 noundef %0) #0 {\n"
                               "  %2 = alloca i32, align 4\n"
                               "  %3 = alloca i32, align 4\n"
                               "  store i32 %0, ptr %2, align 4\n"
                               "  store i32 0, ptr %3, align 4\n"
                               "  %4 = load i32, ptr %2, align 4\n"
                               "  %5 = icmp ne i32 %4, 0\n"
                               "  br i1 %5, label %6, label %16\n"
                               "\n"
                               "6:                                                ; preds = %1\n"
                               "  %7 = load i32, ptr %2, align 4\n"
                               "  %8 = sub i32 %7, 1\n"
                               "  %9 = call i32 @again(i32 noundef %8)\n"
                               "  %10 = load i32, ptr %2, align 4\n"
                               "  %11 = sub i32 %10, 1\n"
                               "  %12 = call i32 @hidden_again(i32 noundef %11)\n"
                               "  %13 = add i32 %9, %12\n"
                               "  %14 = load i32, ptr %2, align 4\n"
                               "  %15 = add i32 %13, %14\n"
                               "  store i32 %15, ptr %3, align 4\n"
                               "  br label %16\n"
                               "\n"
                               "16:                                               ; preds = %6, %1\n"
                               "  %17 = load i32, ptr %3, align 4\n"
                               "  ret i32 %17\n"
                               "}\n"
                               "\n"
                               "define dso_local i32 @use() #0 {\n"
                               "  %1 = call i32 @dispatched()\n"
                               "  ret i32 %1\n"
                               "}\n"
                               "\n"
                               "define internal ptr @resolve() #1 {\n"
                               "  ret ptr @pick\n"
                               "}\n"
                               "\n"
                               "define internal i32 @pick() #0 {\n"
                               "  ret i32 0\n"
                               "}\n"),
    },
};

// Invalid IR, which the verifier would refuse, but the check must not take for a call of nothing. The string holds the
// keyword, which only an alias's own words may.
static const CallGraphInput call_graph_test_lost_alias[] = {
    {
        .name = BUSTER_S8_INIT("lost.ll"),
        .text = BUSTER_S8_INIT("@.str = private unnamed_addr constant [7 x i8] c\" alias \\00\", align 1\n"
                               "@loop_a = dso_local alias void (), ptr @loop_b\n"
                               "@loop_b = internal alias void (), ptr @loop_a\n"
                               "@lost = dso_local alias void (), ptr @nowhere\n"
                               "@empty = dso_local alias void ()\n"
                               "declare void @nowhere()\n"
                               "define dso_local void @f() {\n"
                               "  call void @loop_a()\n"
                               "  call void @lost()\n"
                               "  call void @nowhere()\n"
                               "  ret void\n"
                               "}\n"),
    },
};

// clang 23's IR of these files, as for call_graph_test_alias:
//
//   fallback.c:   __attribute__((weak)) int again(int n) { (void)n; return 0; }
//   strong.c:     int again(int n) { (void)n; return 0; }
//   recurse.c:    int again(int) __attribute__((alias("recurse")));
//                 int recurse(int n)
//                 {
//                     int result = 0;
//                     if (n != 0)
//                     {
//                         result = again(n - 1) + n;
//                     }
//                     return result;
//                 }
//   weak_alias.c: recurse.c with __attribute__((weak, alias("recurse"))).
#define BUSTER_CALL_GRAPH_TEST_AGAIN_BODY                \
    "  %2 = alloca i32, align 4\n"                     \
    "  store i32 %0, ptr %2, align 4\n"                \
    "  %3 = load i32, ptr %2, align 4\n"               \
    "  ret i32 0\n"                                    \
    "}\n"
#define BUSTER_CALL_GRAPH_TEST_FALLBACK "define weak dso_local i32 @again(i32 noundef %0) #0 {\n" BUSTER_CALL_GRAPH_TEST_AGAIN_BODY
#define BUSTER_CALL_GRAPH_TEST_STRONG "define dso_local i32 @again(i32 noundef %0) #0 {\n" BUSTER_CALL_GRAPH_TEST_AGAIN_BODY
#define BUSTER_CALL_GRAPH_TEST_RECURSE_BODY              \
    "  %2 = alloca i32, align 4\n"                     \
    "  %3 = alloca i32, align 4\n"                     \
    "  store i32 %0, ptr %2, align 4\n"                \
    "  store i32 0, ptr %3, align 4\n"                 \
    "  %4 = load i32, ptr %2, align 4\n"               \
    "  %5 = icmp ne i32 %4, 0\n"                       \
    "  br i1 %5, label %6, label %12\n"                \
    "\n"                                               \
    "6:                                                ; preds = %1\n" \
    "  %7 = load i32, ptr %2, align 4\n"               \
    "  %8 = sub i32 %7, 1\n"                           \
    "  %9 = call i32 @again(i32 noundef %8)\n"         \
    "  %10 = load i32, ptr %2, align 4\n"              \
    "  %11 = add i32 %9, %10\n"                        \
    "  store i32 %11, ptr %3, align 4\n"               \
    "  br label %12\n"                                 \
    "\n"                                               \
    "12:                                               ; preds = %6, %1\n" \
    "  %13 = load i32, ptr %3, align 4\n"              \
    "  ret i32 %13\n"                                  \
    "}\n"
#define BUSTER_CALL_GRAPH_TEST_RECURSE                  \
    "@again = dso_local alias i32 (i32), ptr @recurse\n" \
    "\n"                                               \
    "define dso_local i32 @recurse(i32 noundef %0) #0 {\n" BUSTER_CALL_GRAPH_TEST_RECURSE_BODY
#define BUSTER_CALL_GRAPH_TEST_WEAK_ALIAS                \
    "@again = weak dso_local alias i32 (i32), ptr @recurse\n" \
    "\n"                                               \
    "define dso_local i32 @recurse(i32 noundef %0) #0 {\n" BUSTER_CALL_GRAPH_TEST_RECURSE_BODY

static const CallGraphInput call_graph_test_strong_alias[] = {
    {
        .name = BUSTER_S8_INIT("fallback.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_FALLBACK),
    },
    {
        .name = BUSTER_S8_INIT("recurse.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_RECURSE),
    },
};

static const CallGraphInput call_graph_test_strong_alias_reversed[] = {
    {
        .name = BUSTER_S8_INIT("recurse.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_RECURSE),
    },
    {
        .name = BUSTER_S8_INIT("fallback.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_FALLBACK),
    },
};

static const CallGraphInput call_graph_test_weak_alias[] = {
    {
        .name = BUSTER_S8_INIT("strong.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
    {
        .name = BUSTER_S8_INIT("weak_alias.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_WEAK_ALIAS),
    },
};

static const CallGraphInput call_graph_test_weak_alias_reversed[] = {
    {
        .name = BUSTER_S8_INIT("weak_alias.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_WEAK_ALIAS),
    },
    {
        .name = BUSTER_S8_INIT("strong.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
};

static const CallGraphInput call_graph_test_both_strong[] = {
    {
        .name = BUSTER_S8_INIT("strong.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
    {
        .name = BUSTER_S8_INIT("recurse.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_RECURSE),
    },
};

static const CallGraphInput call_graph_test_both_weak[] = {
    {
        .name = BUSTER_S8_INIT("fallback.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_FALLBACK),
    },
    {
        .name = BUSTER_S8_INIT("weak_alias.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_WEAK_ALIAS),
    },
};

// clang 23's IR of these files, as for call_graph_test_alias:
//
//   alias_a.c:  int again(int) __attribute__((alias("recurse")));
//               __attribute__((weak)) int recurse(int n)
//               {
//                   int result = 0;
//                   if (n != 0)
//                   {
//                       result = again(n - 1) + 1;
//                   }
//                   return result;
//               }
//   alias_b.c:  int again(int);
//               int recurse(int n) { return 100 + n; }
//               int main(void) { return again(2) == 2 ? 0 : 1; }
//   inline_a.c: int recurse(int n);
//               extern inline __attribute__((always_inline, gnu_inline)) int again(int n)
//               {
//                   return recurse(n);
//               }
//               int recurse(int n)
//               {
//                   int result = 1;
//                   if (n > 1)
//                   {
//                       result = again(n - 1) + again(n - 2);
//                   }
//                   return result;
//               }
//               int main(void) { return recurse(5) != 8; }
//   inline_b.c: strong.c.
#define BUSTER_CALL_GRAPH_TEST_ALIAS_A                                 \
    "@again = dso_local alias i32 (i32), ptr @recurse\n"               \
    "\n"                                                               \
    "define weak dso_local i32 @recurse(i32 noundef %0) #0 {\n"        \
    "  %2 = alloca i32, align 4\n"                                     \
    "  %3 = alloca i32, align 4\n"                                     \
    "  store i32 %0, ptr %2, align 4\n"                                \
    "  store i32 0, ptr %3, align 4\n"                                 \
    "  %4 = load i32, ptr %2, align 4\n"                               \
    "  %5 = icmp ne i32 %4, 0\n"                                       \
    "  br i1 %5, label %6, label %11\n"                                \
    "\n"                                                               \
    "6:                                                ; preds = %1\n" \
    "  %7 = load i32, ptr %2, align 4\n"                               \
    "  %8 = sub i32 %7, 1\n"                                           \
    "  %9 = call i32 @again(i32 noundef %8)\n"                         \
    "  %10 = add i32 %9, 1\n"                                          \
    "  store i32 %10, ptr %3, align 4\n"                               \
    "  br label %11\n"                                                 \
    "\n"                                                               \
    "11:                                               ; preds = %6, %1\n" \
    "  %12 = load i32, ptr %3, align 4\n"                              \
    "  ret i32 %12\n"                                                  \
    "}\n"
#define BUSTER_CALL_GRAPH_TEST_ALIAS_B                                 \
    "define dso_local i32 @recurse(i32 noundef %0) #0 {\n"             \
    "  %2 = alloca i32, align 4\n"                                     \
    "  store i32 %0, ptr %2, align 4\n"                                \
    "  %3 = load i32, ptr %2, align 4\n"                               \
    "  %4 = add i32 100, %3\n"                                         \
    "  ret i32 %4\n"                                                   \
    "}\n"                                                              \
    "\n"                                                               \
    "define dso_local i32 @main() #0 {\n"                              \
    "  %1 = alloca i32, align 4\n"                                     \
    "  store i32 0, ptr %1, align 4\n"                                 \
    "  %2 = call i32 @again(i32 noundef 2)\n"                          \
    "  %3 = icmp eq i32 %2, 2\n"                                       \
    "  %4 = zext i1 %3 to i64\n"                                       \
    "  %5 = select i1 %3, i32 0, i32 1\n"                              \
    "  ret i32 %5\n"                                                   \
    "}\n"                                                              \
    "\n"                                                               \
    "declare i32 @again(i32 noundef) #1\n"
#define BUSTER_CALL_GRAPH_TEST_INLINE_A                                \
    "define dso_local i32 @recurse(i32 noundef %0) #0 {\n"             \
    "  %2 = alloca i32, align 4\n"                                     \
    "  %3 = alloca i32, align 4\n"                                     \
    "  store i32 %0, ptr %2, align 4\n"                                \
    "  store i32 1, ptr %3, align 4\n"                                 \
    "  %4 = load i32, ptr %2, align 4\n"                               \
    "  %5 = icmp sgt i32 %4, 1\n"                                      \
    "  br i1 %5, label %6, label %14\n"                                \
    "\n"                                                               \
    "6:                                                ; preds = %1\n" \
    "  %7 = load i32, ptr %2, align 4\n"                               \
    "  %8 = sub i32 %7, 1\n"                                           \
    "  %9 = call i32 @again(i32 noundef %8)\n"                         \
    "  %10 = load i32, ptr %2, align 4\n"                              \
    "  %11 = sub i32 %10, 2\n"                                         \
    "  %12 = call i32 @again(i32 noundef %11)\n"                       \
    "  %13 = add i32 %9, %12\n"                                        \
    "  store i32 %13, ptr %3, align 4\n"                               \
    "  br label %14\n"                                                 \
    "\n"                                                               \
    "14:                                               ; preds = %6, %1\n" \
    "  %15 = load i32, ptr %3, align 4\n"                              \
    "  ret i32 %15\n"                                                  \
    "}\n"                                                              \
    "\n"                                                               \
    "define available_externally i32 @again(i32 noundef %0) #1 {\n"    \
    "  %2 = alloca i32, align 4\n"                                     \
    "  store i32 %0, ptr %2, align 4\n"                                \
    "  %3 = load i32, ptr %2, align 4\n"                               \
    "  %4 = call i32 @recurse(i32 noundef %3)\n"                       \
    "  ret i32 %4\n"                                                   \
    "}\n"                                                              \
    "\n"                                                               \
    "define dso_local i32 @main() #0 {\n"                              \
    "  %1 = alloca i32, align 4\n"                                     \
    "  store i32 0, ptr %1, align 4\n"                                 \
    "  %2 = call i32 @recurse(i32 noundef 5)\n"                        \
    "  %3 = icmp ne i32 %2, 8\n"                                       \
    "  %4 = zext i1 %3 to i32\n"                                       \
    "  ret i32 %4\n"                                                   \
    "}\n"

// clang 23's IR of this, as for call_graph_test_alias, with its attribute groups cut to their first words (the group of
// a definition clang may inline holds alwaysinline):
//
//   forced_a.c: int recurse(int);
//               __attribute__((always_inline, weak)) int again(int n)
//               {
//                   return recurse(n);
//               }
//               int recurse(int n)
//               {
//                   int result = 1;
//                   if (n > 1)
//                   {
//                       result = again(n - 1) + again(n - 2);
//                   }
//                   return result;
//               }
//               int main(void) { return recurse(5); }
#define BUSTER_CALL_GRAPH_TEST_FORCED_A                                \
    "define weak dso_local i32 @again(i32 noundef %0) #0 {\n"          \
    "  %2 = alloca i32, align 4\n"                                     \
    "  store i32 %0, ptr %2, align 4\n"                                \
    "  %3 = load i32, ptr %2, align 4\n"                               \
    "  %4 = call i32 @recurse(i32 noundef %3)\n"                       \
    "  ret i32 %4\n"                                                   \
    "}\n"                                                              \
    "\n"                                                               \
    "define dso_local i32 @recurse(i32 noundef %0) #1 {\n"             \
    "  %2 = alloca i32, align 4\n"                                     \
    "  %3 = alloca i32, align 4\n"                                     \
    "  store i32 %0, ptr %2, align 4\n"                                \
    "  store i32 1, ptr %3, align 4\n"                                 \
    "  %4 = load i32, ptr %2, align 4\n"                               \
    "  %5 = icmp sgt i32 %4, 1\n"                                      \
    "  br i1 %5, label %6, label %14\n"                                \
    "\n"                                                               \
    "6:                                                ; preds = %1\n" \
    "  %7 = load i32, ptr %2, align 4\n"                               \
    "  %8 = sub i32 %7, 1\n"                                           \
    "  %9 = call i32 @again(i32 noundef %8)\n"                         \
    "  %10 = load i32, ptr %2, align 4\n"                              \
    "  %11 = sub i32 %10, 2\n"                                         \
    "  %12 = call i32 @again(i32 noundef %11)\n"                       \
    "  %13 = add i32 %9, %12\n"                                        \
    "  store i32 %13, ptr %3, align 4\n"                               \
    "  br label %14\n"                                                 \
    "\n"                                                               \
    "14:                                               ; preds = %6, %1\n" \
    "  %15 = load i32, ptr %3, align 4\n"                              \
    "  ret i32 %15\n"                                                  \
    "}\n"                                                              \
    "\n"                                                               \
    "define dso_local i32 @main() #1 {\n"                              \
    "  %1 = alloca i32, align 4\n"                                     \
    "  store i32 0, ptr %1, align 4\n"                                 \
    "  %2 = call i32 @recurse(i32 noundef 5)\n"                        \
    "  ret i32 %2\n"                                                   \
    "}\n"                                                              \
    "\n"                                                               \
    "attributes #0 = { alwaysinline nounwind sspstrong uwtable \"frame-pointer\"=\"all\" }\n" \
    "attributes #1 = { noinline nounwind optnone sspstrong uwtable \"frame-pointer\"=\"all\" }\n"

static const CallGraphInput call_graph_test_forced[] = {
    {
        .name = BUSTER_S8_INIT("forced_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_FORCED_A),
    },
    {
        .name = BUSTER_S8_INIT("forced_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
};

static const CallGraphInput call_graph_test_forced_reversed[] = {
    {
        .name = BUSTER_S8_INIT("forced_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
    {
        .name = BUSTER_S8_INIT("forced_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_FORCED_A),
    },
};

// clang 23's IR of these, as for call_graph_test_forced:
//
//   flatten_a.c: forced_a.c with `__attribute__((weak))` on again and `__attribute__((flatten))` on recurse, whose calls
//                then carry alwaysinline in their own attribute group;
//   section_a.c: int recurse(int);
//                __attribute__((weak, always_inline, section(" #999 "))) int again(int n) { return recurse(n); }
//                and forced_a.c's recurse.
#define BUSTER_CALL_GRAPH_TEST_FLATTEN_A \
    "define weak dso_local i32 @again(i32 noundef %0) #0 {\n" \
    "  %2 = alloca i32, align 4\n" \
    "  store i32 %0, ptr %2, align 4\n" \
    "  %3 = load i32, ptr %2, align 4\n" \
    "  %4 = call i32 @recurse(i32 noundef %3)\n" \
    "  ret i32 %4\n" \
    "}\n" \
    "\n" \
    "define dso_local i32 @recurse(i32 noundef %0) #0 {\n" \
    "  %2 = alloca i32, align 4\n" \
    "  %3 = alloca i32, align 4\n" \
    "  store i32 %0, ptr %2, align 4\n" \
    "  store i32 1, ptr %3, align 4\n" \
    "  %4 = load i32, ptr %2, align 4\n" \
    "  %5 = icmp sgt i32 %4, 1\n" \
    "  br i1 %5, label %6, label %14\n" \
    "\n" \
    "6:                                                ; preds = %1\n" \
    "  %7 = load i32, ptr %2, align 4\n" \
    "  %8 = sub i32 %7, 1\n" \
    "  %9 = call i32 @again(i32 noundef %8) #1\n" \
    "  %10 = load i32, ptr %2, align 4\n" \
    "  %11 = sub i32 %10, 2\n" \
    "  %12 = call i32 @again(i32 noundef %11) #1\n" \
    "  %13 = add i32 %9, %12\n" \
    "  store i32 %13, ptr %3, align 4\n" \
    "  br label %14\n" \
    "\n" \
    "14:                                               ; preds = %6, %1\n" \
    "  %15 = load i32, ptr %3, align 4\n" \
    "  ret i32 %15\n" \
    "}\n" \
    "\n" \
    "define dso_local i32 @main() #0 {\n" \
    "  %1 = alloca i32, align 4\n" \
    "  store i32 0, ptr %1, align 4\n" \
    "  %2 = call i32 @recurse(i32 noundef 5)\n" \
    "  ret i32 %2\n" \
    "}\n" \
    "\n" \
    "attributes #0 = { noinline nounwind optnone sspstrong uwtable }\n" \
    "attributes #1 = { alwaysinline }\n"
#define BUSTER_CALL_GRAPH_TEST_SECTION_A \
    "define weak dso_local i32 @again(i32 noundef %0) #0 section \" #999 \" {\n" \
    "  %2 = alloca i32, align 4\n" \
    "  store i32 %0, ptr %2, align 4\n" \
    "  %3 = load i32, ptr %2, align 4\n" \
    "  %4 = call i32 @recurse(i32 noundef %3)\n" \
    "  ret i32 %4\n" \
    "}\n" \
    "\n" \
    "define dso_local i32 @recurse(i32 noundef %0) #1 {\n" \
    "  %2 = alloca i32, align 4\n" \
    "  %3 = alloca i32, align 4\n" \
    "  store i32 %0, ptr %2, align 4\n" \
    "  store i32 1, ptr %3, align 4\n" \
    "  %4 = load i32, ptr %2, align 4\n" \
    "  %5 = icmp sgt i32 %4, 1\n" \
    "  br i1 %5, label %6, label %14\n" \
    "\n" \
    "6:                                                ; preds = %1\n" \
    "  %7 = load i32, ptr %2, align 4\n" \
    "  %8 = sub i32 %7, 1\n" \
    "  %9 = call i32 @again(i32 noundef %8)\n" \
    "  %10 = load i32, ptr %2, align 4\n" \
    "  %11 = sub i32 %10, 2\n" \
    "  %12 = call i32 @again(i32 noundef %11)\n" \
    "  %13 = add i32 %9, %12\n" \
    "  store i32 %13, ptr %3, align 4\n" \
    "  br label %14\n" \
    "\n" \
    "14:                                               ; preds = %6, %1\n" \
    "  %15 = load i32, ptr %3, align 4\n" \
    "  ret i32 %15\n" \
    "}\n" \
    "\n" \
    "attributes #0 = { alwaysinline nounwind sspstrong uwtable }\n" \
    "attributes #1 = { noinline nounwind optnone sspstrong uwtable }\n"

static const CallGraphInput call_graph_test_flatten[] = {
    {
        .name = BUSTER_S8_INIT("flatten_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_FLATTEN_A),
    },
    {
        .name = BUSTER_S8_INIT("flatten_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
};

static const CallGraphInput call_graph_test_flatten_reversed[] = {
    {
        .name = BUSTER_S8_INIT("flatten_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
    {
        .name = BUSTER_S8_INIT("flatten_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_FLATTEN_A),
    },
};

static const CallGraphInput call_graph_test_section[] = {
    {
        .name = BUSTER_S8_INIT("section_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_SECTION_A),
    },
    {
        .name = BUSTER_S8_INIT("section_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
};

// g and w are weak, and elsewhere.ll's strong definitions replace them. f's call reaches its own unit's g too, which
// may run inlined, but c's call in elsewhere.ll reaches only the strong w: bodies.ll's w, which calls c, never runs.
static const CallGraphInput call_graph_test_replaced_bodies[] = {
    {
        .name = BUSTER_S8_INIT("bodies.ll"),
        .text = BUSTER_S8_INIT("define weak void @g() {\n"
                               "  call void @f()\n"
                               "  ret void\n"
                               "}\n"
                               "define dso_local void @f() {\n"
                               "  call void @g()\n"
                               "  ret void\n"
                               "}\n"
                               "define weak void @w() {\n"
                               "  call void @c()\n"
                               "  ret void\n"
                               "}\n"),
    },
    {
        .name = BUSTER_S8_INIT("elsewhere.ll"),
        .text = BUSTER_S8_INIT("define dso_local void @g() {\n"
                               "  ret void\n"
                               "}\n"
                               "define dso_local void @w() {\n"
                               "  ret void\n"
                               "}\n"
                               "define dso_local void @c() {\n"
                               "  call void @w()\n"
                               "  ret void\n"
                               "}\n"),
    },
};

static const CallGraphInput call_graph_test_replaced_aliasee[] = {
    {
        .name = BUSTER_S8_INIT("alias_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_ALIAS_A),
    },
    {
        .name = BUSTER_S8_INIT("alias_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_ALIAS_B),
    },
};

static const CallGraphInput call_graph_test_replaced_aliasee_reversed[] = {
    {
        .name = BUSTER_S8_INIT("alias_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_ALIAS_B),
    },
    {
        .name = BUSTER_S8_INIT("alias_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_ALIAS_A),
    },
};

static const CallGraphInput call_graph_test_inline_body[] = {
    {
        .name = BUSTER_S8_INIT("inline_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_INLINE_A),
    },
    {
        .name = BUSTER_S8_INIT("inline_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
};

static const CallGraphInput call_graph_test_inline_body_reversed[] = {
    {
        .name = BUSTER_S8_INIT("inline_b.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_STRONG),
    },
    {
        .name = BUSTER_S8_INIT("inline_a.ll"),
        .text = BUSTER_S8_INIT(BUSTER_CALL_GRAPH_TEST_INLINE_A),
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

static const CallGraphInput call_graph_test_same_names[] = {
    {
        .name = BUSTER_S8_INIT("one.ll"),
        .text = BUSTER_S8_INIT("define dso_local void @x() {\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @a() {\n"
                               "  call void @y()\n"
                               "  ret void\n"
                               "}\n"),
    },
    {
        .name = BUSTER_S8_INIT("two.ll"),
        .text = BUSTER_S8_INIT("define dso_local void @y() {\n"
                               "  call void @a()\n"
                               "  ret void\n"
                               "}\n"
                               "define internal void @a() {\n"
                               "  call void @x()\n"
                               "  ret void\n"
                               "}\n"),
    },
};

// The first two have the component's size and none of its names is missing, but they name `a` once; the third names it
// twice.
static const CallGraphAllowed call_graph_test_same_names_allowed[] = {
    {
        .functions = BUSTER_S8_INIT("x a y b"),
    },
    {
        .functions = BUSTER_S8_INIT("x a y y"),
    },
    {
        .functions = BUSTER_S8_INIT("a y a x"),
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
        .name = "aliases and an ifunc",
        .inputs = call_graph_test_alias,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_alias),
        .expected = BUSTER_S8_INIT("alias.ll:34: a call of dispatched, an ifunc, whose callee is chosen at run time\n"
                                   "cycle: alias.ll:recurse -> alias.ll:recurse\n"
                                   "call graph: 1 translation unit, 4 functions, 2 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "aliases of nothing",
        .inputs = call_graph_test_lost_alias,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_lost_alias),
        .expected = BUSTER_S8_INIT("lost.ll:5: an alias without a target\n"
                                   "lost.ll:8: a call of loop_a, an alias that leads to no definition\n"
                                   "lost.ll:9: a call of lost, an alias that leads to no definition\n"
                                   "call graph: 1 translation unit, 1 function, 0 calls between them, 0 cycles, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "weak function, strong alias",
        .inputs = call_graph_test_strong_alias,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_strong_alias),
        .expected = BUSTER_S8_INIT("cycle: recurse.ll:recurse -> recurse.ll:recurse\n"
                                   "call graph: 2 translation units, 2 functions, 1 call between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "strong alias, weak function",
        .inputs = call_graph_test_strong_alias_reversed,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_strong_alias_reversed),
        .expected = BUSTER_S8_INIT("cycle: recurse.ll:recurse -> recurse.ll:recurse\n"
                                   "call graph: 2 translation units, 2 functions, 1 call between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "strong function, weak alias",
        .inputs = call_graph_test_weak_alias,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_weak_alias),
        .expected = BUSTER_S8_INIT("cycle: weak_alias.ll:recurse -> weak_alias.ll:recurse\n"
                                   "call graph: 2 translation units, 2 functions, 2 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "weak alias, strong function",
        .inputs = call_graph_test_weak_alias_reversed,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_weak_alias_reversed),
        .expected = BUSTER_S8_INIT("cycle: weak_alias.ll:recurse -> weak_alias.ll:recurse\n"
                                   "call graph: 2 translation units, 2 functions, 2 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "two strong definitions",
        .inputs = call_graph_test_both_strong,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_both_strong),
        .expected = BUSTER_S8_INIT("recurse.ll:1: a second strong definition of again\n"
                                   "cycle: recurse.ll:recurse -> recurse.ll:recurse\n"
                                   "call graph: 2 translation units, 2 functions, 2 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "two weak definitions",
        .inputs = call_graph_test_both_weak,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_both_weak),
        .expected = BUSTER_S8_INIT("cycle: weak_alias.ll:recurse -> weak_alias.ll:recurse\n"
                                   "call graph: 2 translation units, 2 functions, 2 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "replaced aliasee",
        .inputs = call_graph_test_replaced_aliasee,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_replaced_aliasee),
        .expected = BUSTER_S8_INIT("cycle: alias_a.ll:recurse -> alias_a.ll:recurse\n"
                                   "call graph: 2 translation units, 3 functions, 2 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "replaced aliasee, reversed",
        .inputs = call_graph_test_replaced_aliasee_reversed,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_replaced_aliasee_reversed),
        .expected = BUSTER_S8_INIT("cycle: alias_a.ll:recurse -> alias_a.ll:recurse\n"
                                   "call graph: 2 translation units, 3 functions, 2 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "inline body",
        .inputs = call_graph_test_inline_body,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_inline_body),
        .expected = BUSTER_S8_INIT("cycle: inline_a.ll:recurse -> inline_a.ll:again -> inline_a.ll:recurse\n"
                                   "call graph: 2 translation units, 4 functions, 6 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "inline body, reversed",
        .inputs = call_graph_test_inline_body_reversed,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_inline_body_reversed),
        .expected = BUSTER_S8_INIT("cycle: inline_a.ll:recurse -> inline_a.ll:again -> inline_a.ll:recurse\n"
                                   "call graph: 2 translation units, 4 functions, 6 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "forced weak body",
        .inputs = call_graph_test_forced,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_forced),
        .expected = BUSTER_S8_INIT("cycle: forced_a.ll:again -> forced_a.ll:recurse -> forced_a.ll:again\n"
                                   "call graph: 2 translation units, 4 functions, 6 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "forced weak body, reversed",
        .inputs = call_graph_test_forced_reversed,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_forced_reversed),
        .expected = BUSTER_S8_INIT("cycle: forced_a.ll:again -> forced_a.ll:recurse -> forced_a.ll:again\n"
                                   "call graph: 2 translation units, 4 functions, 6 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "flatten caller",
        .inputs = call_graph_test_flatten,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_flatten),
        .expected = BUSTER_S8_INIT("cycle: flatten_a.ll:again -> flatten_a.ll:recurse -> flatten_a.ll:again\n"
                                   "call graph: 2 translation units, 4 functions, 6 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "flatten caller, reversed",
        .inputs = call_graph_test_flatten_reversed,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_flatten_reversed),
        .expected = BUSTER_S8_INIT("cycle: flatten_a.ll:again -> flatten_a.ll:recurse -> flatten_a.ll:again\n"
                                   "call graph: 2 translation units, 4 functions, 6 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "section name",
        .inputs = call_graph_test_section,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_section),
        .expected = BUSTER_S8_INIT("cycle: section_a.ll:again -> section_a.ll:recurse -> section_a.ll:again\n"
                                   "call graph: 2 translation units, 3 functions, 5 calls between them, 1 cycle, 0 allowed\n"),
        .success = false,
    },
    {
        .name = "replaced bodies",
        .inputs = call_graph_test_replaced_bodies,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_replaced_bodies),
        .expected = BUSTER_S8_INIT("cycle: bodies.ll:g -> bodies.ll:f -> bodies.ll:g\n"
                                   "call graph: 2 translation units, 6 functions, 5 calls between them, 1 cycle, 0 allowed\n"),
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
        .name = "names that repeat",
        .inputs = call_graph_test_same_names,
        .input_count = BUSTER_ARRAY_LENGTH(call_graph_test_same_names),
        .allowed = call_graph_test_same_names_allowed,
        .allowed_count = BUSTER_ARRAY_LENGTH(call_graph_test_same_names_allowed),
        .expected = BUSTER_S8_INIT("allowed cycle: one.ll:x -> one.ll:a -> two.ll:y -> two.ll:a -> one.ll:x\n"
                                   "allowlist entry without a cycle: x a y b\n"
                                   "allowlist entry without a cycle: x a y y\n"
                                   "call graph: 2 translation units, 4 functions, 4 calls between them, 0 cycles, 1 allowed\n"),
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
