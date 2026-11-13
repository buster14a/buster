typedef struct { char* pointer; unsigned long length; } S8;
struct W { S8 s; S8 t[2]; };
int f(const S8* x, struct W w, struct W** q)
{
    const S8 a[] = { x[1], x[2] };
    _Static_assert(sizeof a == 2 * sizeof(S8), "two");
    S8 b[] = { x[0], { "c", 1 }, x[1] };
    _Static_assert(sizeof b == 3 * sizeof(S8), "three");
    S8 c[] = { *x, (*x), w.s, w.t[1], q[0]->s, (*q)->t[0] };
    _Static_assert(sizeof c == 6 * sizeof(S8), "six");
    unsigned long d[] = { x[1].length, w.t[0].length, q[1]->s.length };
    _Static_assert(sizeof d == 3 * sizeof(unsigned long), "three scalars");
    return (int)(a[1].length + b[2].length + c[5].length + d[2]);
}
