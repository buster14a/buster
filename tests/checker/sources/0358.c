typedef struct { int a, b; } S;
struct C { S s[2]; struct C* next; };
int f(S*************** p, S**************** q, S***************** r, struct C* x)
{
    S a[] = { ***************p, ***************p };
    _Static_assert(sizeof a == 2 * sizeof(S), "15");
    S b[] = { ****************q, ****************q };
    _Static_assert(sizeof b == 2 * sizeof(S), "16");
    S c[] = { *****************r, *****************r };
    _Static_assert(sizeof c == 2 * sizeof(S), "17");
    S d[] = { x->next->next->next->next->next->next->next->next->next->next->next->next->next->next->s[1],
              x->next->next->next->next->next->next->next->next->next->next->next->next->next->next->next->s[0], x->s[1] };
    _Static_assert(sizeof d == 3 * sizeof(S), "16 and 17 members and subscripts");
    return a[1].a + b[1].a + c[1].a + d[2].b;
}
