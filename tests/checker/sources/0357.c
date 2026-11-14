typedef struct { int a, b; } S;
struct W { S s; S t[2]; };
int f(S* p, int i, struct W* w, S** q)
{
    S a[] = { 0[p], 1[p] };
    _Static_assert(sizeof a == 2 * sizeof(S), "two");
    S b[] = { p[0], i[p], 1[w->t], 0[q][1], 1[0[q]], (i[q])[i] };
    _Static_assert(sizeof b == 6 * sizeof(S), "six");
    int c[] = { 0[p].a, i[p].b, (1[w->t]).a };
    _Static_assert(sizeof c == 3 * sizeof(int), "three scalars");
    return a[1].b + b[5].a + c[2];
}
