// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wunused-but-set-parameter"
int mixed(int branch, int overwritten)
{
    int unread;
    overwritten = 3;
    if (branch)
    {
        return 18 + 24;
    }
    return unread + 1;
}

#pragma clang diagnostic pop

int conditional(int condition)
{
    if (condition)
    {
        return 2;
    }
    return 4 / 0;
}

int loop(int bound)
{
    int i = 0;
    for (; i < bound;)
    {
        (void)(bound + 1);
    }
    return 7;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdivision-by-zero"
int muted(void) { return 1 / 0; }
#pragma clang diagnostic pop

int restored(void) { return 3 / 0; }

int local_uninitialized(void)
{
    int value;
    return +value;
}

int initialized_parameter(int parameter, int new_value)
{
    parameter = new_value;
    return parameter;
}

#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wunused-but-set-parameter"
int syntactic_use(int parameter)
{
    parameter = 4;
    return 8;
    return parameter;
}
#pragma clang diagnostic pop

int bare_self_initialize(void)
{
    int value = value;
    return value;
}

int earliest(int branch)
{
    int value;
    if (branch)
    {
        return +value;
    }
    return value;
}

int assigned_uninitialized(void)
{
    int missing;
    int target;
    target = missing;
    return target;
}

int assigned_loop(int bound)
{
    int i = 0;
    for (; i < bound;)
    {
        i = bound;
    }
    return i;
}

int self_assignment(int parameter)
{
    parameter = parameter;
    return parameter;
}

int statement_result(int parameter)
{
    return ({ parameter = 2; });
}

int braced_after_write(void)
{
    int value = { ({ value = 2; value; }) };
    return value;
}

int labeled_statement_results(int parameter)
{
#define BUSTER_LABELED_RESULT(condition) ({ __label__ done; int x = 0; if (condition) goto done; x = 1; done: x; })
    return BUSTER_LABELED_RESULT(parameter) + BUSTER_LABELED_RESULT(parameter > 1);
#undef BUSTER_LABELED_RESULT
}
