typedef int R __attribute__((warn_unused_result));
struct __attribute__((warn_unused_result)) S { int x; };
typedef R (*FP)(void);
struct T { R (*m)(void); struct S (*sm)(void); } t, *pt;
R (*arr[2])(void);
FP get(void);
void f(int n, R (**pp)(void), FP a, FP b, struct S (*sp)(void))
{
    t.m();
    pt->sm();
    arr[0]();
    get()();
    (n, a)();
    (n ? a : b)();
    (*pp)();
    sp();
    a();
}
