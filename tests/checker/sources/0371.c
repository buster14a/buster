typedef struct { char* pointer; unsigned long length; } S8;
struct W { S8 s; S8 t[2]; };
int f(const S8* x)
{
    S8 a[] = { x->pointer, x[1] };
    return (int)sizeof a;
}
