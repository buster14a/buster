// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// measure-tier0-entries' fixture (test-checker's check_test_tier0_entries, tier0_entries_measure_test.c): original C
// whose functions make each way the census finds a record pattern. Templates with their fusions (a constant, a local,
// two locals compared, a condition's test and its branch, a pointer addition into a load and a store), a call and a
// copy (deferred entries), a division with its operands in rax and elsewhere, floating values, a global, two pointer
// additions whose constants are of one class and whose scaled constants take an 8-bit and a 32-bit immediate, and the
// conditions' ways (#2877): a compare behind its test against zero (the loop's), a negated one behind two, a twice
// negated one, whose first test's value is made and tested again, and a long double compare, whose value is too.
struct Row
{
    long first;
    long second;
    long third;
    long fourth;
    long fifth;
};

int external(int, int);

static int counter;

int sum(const int* values, int count)
{
    int total = 0;
    for (int i = 0; i < count; i += 1)
    {
        total += values[i];
    }
    return total;
}

int scale(int x)
{
    return x * 3 + 1;
}

int call(int a, int b)
{
    return external(a, b + 1) + counter;
}

long divide(long a, long b, long c)
{
    return a / b + c / (a + 1);
}

void copy(struct Row* to, const struct Row* from)
{
    *to = *from;
}

double mix(double x, int n)
{
    return x * n + 0.5;
}

void store(int* p, int i, int v)
{
    p[i] = v;
    p[3] = 7;
    counter = v;
}

long* third(long* p)
{
    return p + 3;
}

long* twentieth(long* p)
{
    return p + 20;
}

int negated(int a, int b)
{
    if (!(a < b))
    {
        return 1;
    }
    return 2;
}

int twice_negated(int a, int b)
{
    if (!!(a < b))
    {
        return 1;
    }
    return 2;
}

int extended(long double a, long double b)
{
    if (a < b)
    {
        return 1;
    }
    return 2;
}
