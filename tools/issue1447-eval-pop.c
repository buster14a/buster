// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original diagnostic oracle for eval_method history across float_control stack operations.
enum { BASE = sizeof(+1.0f) };
typedef float T __attribute__((available_only_in_default_eval_method));
typedef float float_t;
#if CASE == 1
#pragma float_control(push)
#pragma clang fp eval_method(double)
#pragma float_control(pop)
_Static_assert(sizeof(+1.0f)==BASE,"restored arithmetic");
T value;
#elif CASE == 2
#pragma float_control(push)
#pragma clang fp eval_method(double)
#pragma float_control(pop)
#pragma clang fp eval_method(source)
_Static_assert(sizeof(+1.0f)==4,"source arithmetic");
T value;
#elif CASE == 3
#pragma clang fp eval_method(double)
#pragma float_control(push)
#pragma clang fp eval_method(source)
#pragma float_control(pop)
_Static_assert(sizeof(+1.0f)==8,"double arithmetic");
T value;
#elif CASE == 4
#pragma clang fp eval_method(double)
void inner(void) {
#pragma clang fp eval_method(extended)
_Static_assert(sizeof(+1.0f)==16,"inner arithmetic");
}
_Static_assert(sizeof(+1.0f)==8,"outer arithmetic");
T value;
#elif CASE == 5
#pragma float_control(push)
#pragma clang fp eval_method(double)
#pragma float_control(push)
#pragma clang fp eval_method(extended)
#pragma float_control(pop)
#pragma float_control(pop)
_Static_assert(sizeof(+1.0f)==BASE,"twice restored arithmetic");
T value;
#elif CASE == 6
#pragma float_control(push)
#pragma float_control(precise, on)
#pragma float_control(pop)
_Static_assert(sizeof(+1.0f)==BASE,"unchanged arithmetic");
T value;
#elif CASE == 7
#pragma float_control(push)
#pragma clang fp eval_method(double)
_Pragma("float_control(pop)")
_Static_assert(sizeof(+1.0f)==BASE,"macro pop arithmetic");
float_t value;
#elif CASE == 8
#pragma float_control(push)
#pragma clang fp eval_method(double)
#pragma float_control(pop)
int builtin = __FLT_EVAL_METHOD__;
#elif CASE == 9
#pragma float_control(push)
#pragma clang fp eval_method(double)
#pragma float_control(pop)
void inner(void) { T value; (void)value; }
#elif CASE == 10
#pragma float_control(push)
#pragma clang fp eval_method(double)
#pragma float_control(pop)
void inner(void) {
#pragma clang fp eval_method(source)
T value; (void)value;
}
T outside;
#endif
