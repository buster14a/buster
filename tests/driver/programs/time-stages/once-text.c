#define STORE(p, v) (*(p) = (v))
#define BUMP(p) ((*(p))++)
#define CALL(f, p) f(p)
#define DROP(v) ((void)(v))
int h(int* p);
int f(int* p)
{
    STORE(p, 1);
    BUMP(p);
    CALL(h, p);
    DROP(*p);
    return *p;
}
