typedef int I;
struct u { int a; };
typedef struct u uu;
typedef uu *uup;
typedef int v4si __attribute__((vector_size(16)));
struct s
{
    uu f;
    uup g;
    I arr[2];
    struct { uu deep; I *ip; };
    union { v4si v; float fl; } un;
    const uu cf;
    struct s *next;
};
void g(struct s x, struct s *px, const struct s cx)
{
    x.f++;
    px->f++;
    (*x.g)++;
    x.g->a();
    x.arr[0]();
    x.deep--;
    (*px->ip)();
    x.un.v++;
    cx.f++;
    x.cf++;
    px->next->next->f++;
    (x).f();
    x.next->ip();
}
