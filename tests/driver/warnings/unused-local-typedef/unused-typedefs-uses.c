#define DECL(n) typedef int n
int f(int n)
{
    typedef int A; typedef A B;
    typedef long C; int s = sizeof(C);
    typedef short D; int t = (D)n;
    typedef char E; { E e = 0; (void)e; }
    typedef int F; void g(F);
    typedef int __attribute__((unused)) G;
    DECL(H);
    typedef struct { int x; } I; int z = ({ I i = { 1 }; i.x; });
    typedef int J[sizeof(int)];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-local-typedef"
    typedef int K;
#pragma clang diagnostic pop
    typedef int L, M; L l = 0;
    return s + t + z + l;
}
