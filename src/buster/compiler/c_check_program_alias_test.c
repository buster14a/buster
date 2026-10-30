// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's alias, ifunc and weakref attributes (#147), run against clang -O0's build of the same programs: aliases of
// static functions and objects (thread-local ones too), global and local, and chains of them, with static data that
// refers to local ones before they have their places (check_data_emit's local targets); ifuncs whose resolvers
// return a function pointer and a void pointer, called directly and through pointers; and weakrefs to the harness's
// function and object (check_test_program_harness's external_helper and external_data), to a name nothing defines, to a
// static function and to a static declaration nothing defines, whose weak reference is a global symbol's.
// The oracle renames the program's global definitions (check_test_program_oracle), so each attribute's name is a
// static's, which both objects keep apart (an ifunc is global, so no alias names one), and only a function, whose copies
// compute alike, has a weak alias, which the link takes the oracle's of. Include after c_check_program_test.c's source
// and execution helpers.

static const char check_test_program_alias_functions[] =
    "static int implementation(int n){return n*3+1;}"
    "int exported(int) __attribute__((alias(\"implementation\")));"
    "static int local_copy(int) __attribute__((alias(\"implementation\")));"
    "static int local_chain(int) __attribute__((alias(\"local_copy\")));"
    "int weak_exported(int) __attribute__((weak, alias(\"local_chain\")));"
    "int (*const table[])(int) = { exported, local_copy, weak_exported };"
    "int tested(int n){int (*p)(int)=local_chain;return exported(n)+local_copy(n)*3+local_chain(n)*5+weak_exported(n)*7+p(n)*11+table[(unsigned)n%3](n)*13;}";

static const char check_test_program_alias_objects[] =
    "static int values[4] = { 1, 2, 3, 4 };"
    "extern int value_view[4] __attribute__((alias(\"values\")));"
    "static long counter;"
    "static long counter_copy __attribute__((alias(\"counter\")));"
    "extern long counter_view __attribute__((alias(\"counter_copy\")));"
    "static _Thread_local int thread_value = 5;"
    "extern _Thread_local int thread_view __attribute__((alias(\"thread_value\")));"
    "static const char text[] = \"aliased\";"
    "extern const char text_view[8] __attribute__((alias(\"text\")));"
    "int* const pointer = value_view;"
    "long* const counter_pointer = &counter_copy;"
    "int tested(int n){value_view[(unsigned)n&3]+=n;counter_view+=n;thread_view+=n;"
    "return values[(unsigned)n&3]+(int)counter*3+thread_value*5+text_view[(unsigned)n%7]*7+pointer[1]*11+(int)counter_copy*13+(int)*counter_pointer*17;}";

static const char check_test_program_alias_ifuncs[] =
    "static int add(int n){return n+7;}"
    "static int (*pick(void))(int){return add;}"
    "int dispatched(int) __attribute__((ifunc(\"pick\")));"
    "static int twice(int n){return n*2;}"
    "static void* pick_twice(void){return (void*)twice;}"
    "int weak_dispatched(int) __attribute__((ifunc(\"pick_twice\"), weak));"
    "static int local_dispatch(int) __attribute__((ifunc(\"pick\")));"
    "int tested(int n){int (*p)(int)=dispatched;int (*q)(int)=weak_dispatched;"
    "return dispatched(n)+p(n)*3+weak_dispatched(n)*5+q(n)*7+local_dispatch(n)*11+(p==dispatched)*17;}";

static const char check_test_program_alias_weakrefs[] =
    "static int helper(int) __attribute__((weakref(\"external_helper\")));"
    "static int helper_again(int) __attribute__((weakref, alias(\"external_helper\")));"
    "static int missing(void) __attribute__((weakref(\"check_test_alias_missing\")));"
    "static int missing_again(void) __attribute__((weakref(\"check_test_alias_missing\")));"
    "static int data __attribute__((weakref(\"external_data\")));"
    "static int local_target(int n){return n-3;}"
    "static int local(int) __attribute__((weakref(\"local_target\")));"
    "static int declared_only(void);"
    "static int weak_declared(void) __attribute__((weakref(\"declared_only\")));"
    "int tested(int n){int (*p)(int)=helper;"
    "return helper(n)+helper_again(n)*3+(missing?11:13)*5+(missing_again==0)*7+data*11+local(n)*13+p(n)*17+(&data!=0)*19+(weak_declared?3:5)*23;}";

static void check_test_program_aliases(CheckTest* test, const char* clang)
{
    check_test_program_source(test, check_test_program_alias_functions, "alias-functions", clang);
    check_test_program_source(test, check_test_program_alias_objects, "alias-objects", clang);
    check_test_program_source(test, check_test_program_alias_ifuncs, "alias-ifuncs", clang);
    check_test_program_source(test, check_test_program_alias_weakrefs, "alias-weakrefs", clang);
}
