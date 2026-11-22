// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original declaration-recovery programs for #2332. Kept as runtime data so the compiler under test reads the same
// source as the oracle, including every original diagnostic place and previous-definition note.

#if BUSTER_CASE == 0
void grouped_parameter(int x)
{
    __attribute__((unused)) (x)++;
    x == 3;
    missing_after();
}
#elif BUSTER_CASE == 1
void pointer_parameter(int *p)
{
    __attribute__((unused)) *p = 1;
    p[0];
    missing_after();
}
#elif BUSTER_CASE == 2
void new_pointer(void)
{
    __attribute__((unused)) *p = 1;
    *p;
    missing_after();
}
#elif BUSTER_CASE == 3
void local_definition(void)
{
    int x = 2;
    __attribute__((unused)) ((x))++;
    x == 3;
    missing_after();
}
#elif BUSTER_CASE == 4
void declarator_list(int *p)
{
    __attribute__((unused)) (fresh), *p = "text";
    fresh;
    *p;
    missing_after();
}
#elif BUSTER_CASE == 5
void qualified_pointer(int *p)
{
    __attribute__((unused)) *const p = 1;
    *p;
    missing_after();
}
#elif BUSTER_CASE == 6
void grouped_pointer(void)
{
    __attribute__((unused)) ((*const p)) = 0;
    *p;
    missing_after();
}
#elif BUSTER_CASE == 7
void initializer_expression(int *p)
{
    __attribute__((unused)) *p = (1 / 0, 1);
    __attribute__((unused)) *p = "left" + "right";
    *p;
    missing_after();
}
#elif BUSTER_CASE == 8
void initializer_name(int *p)
{
    __attribute__((unused)) *p = initializer_missing;
    *p;
    missing_after();
}
#elif BUSTER_CASE == 9
void balanced_suffix(int x)
{
    __attribute__((unused)) (x)++[1 + (2)];
    x == 3;
    missing_after();
}
#elif BUSTER_CASE == 10
void unbalanced_suffix(void)
{
    __attribute__((unused)) (x)++(;
}
#elif BUSTER_CASE == 11
void brace_boundary(void)
{
    __attribute__((unused)) (x)++ { }
}
#elif BUSTER_CASE == 12
void repeated_attribute(int x __attribute__((unused)))
{
    __attribute__((unused)) (x)++;
    missing_after();
}
#elif BUSTER_CASE == 13
int main(void)
{
    int x = 7;
    __attribute__((unused)) int (y) = x;
    __attribute__((unused)) int (*const p) = &y, *q = &x;
    return *p != 7 || *q != 7;
}
#elif BUSTER_CASE == 14
void mismatched_parenthesis(void)
{
    __attribute__((unused)) (fresh)++(]; after;
}
#elif BUSTER_CASE == 15
void mismatched_bracket(void)
{
    __attribute__((unused)) (fresh)++[); after;
}
#elif BUSTER_CASE == 16
struct Record { int member; };
void invalid_initializer_operand(void)
{
    __attribute__((unused)) *p = +(struct Record){0};
    *p;
    missing_after();
}
#elif BUSTER_CASE == 17
int called(int);
void invalid_initializer_call(int *p)
{
    __attribute__((unused)) *p = called(1, 2);
    *p;
    missing_after();
}
#elif BUSTER_CASE == 18
void nested_invalid_binding(int *p)
{
    {
        __attribute__((unused)) *p = 0;
        *p;
    }
    p[0];
    missing_after();
}
#elif BUSTER_CASE == 19
void mismatched_brace(void)
{
    __attribute__((unused)) (fresh)++(};
int following(void) { return 0; }
#elif BUSTER_CASE == 20
void preserved_typedef_pointer(void)
{
    typedef int kept;
    __attribute__((unused)) *kept = 0;
    kept variable = 1;
    {
        __attribute__((unused)) *kept = 0;
        (void)kept;
    }
    kept after = variable;
    (void)after;
    missing_after();
}
#elif BUSTER_CASE == 21
void preserved_typedef_group(void)
{
    typedef int kept;
    __attribute__((unused)) (kept)++;
    kept variable = 1;
    (void)variable;
    missing_after();
}
#elif BUSTER_CASE == 22
struct FunctionRecord { int member; };
int invalid_function_initializer(void)
{
    __attribute__((unused)) (fresh)(void) = +(struct FunctionRecord){0};
    return fresh() + undeclared_after;
}
#else
#error unknown BUSTER_CASE
#endif
