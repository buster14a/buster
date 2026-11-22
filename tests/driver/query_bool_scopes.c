// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Top-level, member and deferred body checks each retain their own source facts, without repeated warnings.
struct Scoped
{
    _Static_assert(sizeof((1 << 2) ? 3 : 4) != 0, "member query");
    int values[(1 << 2) ? 3 : 4];
};
int first_body(void)
{
    typedef int Local[(1 << 2) ? 3 : 4];
    typedef __typeof__(0.5 ? 3 : 4) Query;
    _Static_assert(_Generic((1 << 2) ? 3 : 4, int: 1), "local selection");
    return sizeof(Local) + sizeof(Query) + sizeof((1 << 2) ? 3 : 4);
}
int second_body(void)
{
    _Static_assert(sizeof(__typeof__((1 << 2) ? 3 : 4)) != 0, "local typeof");
    return _Generic(0.5 ? 3 : 4, int: 1, default: (1 << 2) ? 3 : 4);
}
int third_body(void)
{
    return sizeof(int[(1 << 2) ? 3 : 4]);
}
