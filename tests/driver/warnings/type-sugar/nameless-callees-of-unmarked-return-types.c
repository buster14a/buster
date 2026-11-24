typedef int R __attribute__((warn_unused_result));
typedef int Q;
struct T { Q (*m)(void); R *(*rp)(void); } t;
Q (*arr[2])(void);
R *(*parr[2])(void);
Q (*get(void))(void);
void f(int n, Q (**pp)(void), R *(**rpp)(void))
{
    t.m(); t.rp(); arr[0](); parr[1](); get()(); (*pp)(); (*rpp)(); (n, arr[0])();
}
