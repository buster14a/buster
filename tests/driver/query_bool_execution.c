// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Retaining a query's diagnostic facts must not execute its operand or an unselected association.
volatile int calls;
int touch(void)
{
    calls += 1;
    return 1;
}
int main(void)
{
    int value = sizeof((touch() << 2) ? 3 : 4);
    value += _Generic((touch() << 2) ? 3 : 4, int: 7, default: touch());
    typedef __typeof__((touch() << 2) ? 3 : 4) Query;
    int selected = _Generic(0, int: (1 << 2) ? 3 : 4, default: touch());
    return calls != 0 || value != 11 || sizeof(Query) != 4 || selected != 3;
}
