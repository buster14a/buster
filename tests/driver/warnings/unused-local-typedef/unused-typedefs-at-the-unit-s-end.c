void f(int n)
{
    typedef int Z;
    int unused1;
    {
        typedef int Inner;
        int unused2;
    }
    typedef int Used;
    Used u = 0;
    (void)u;
    typedef struct S { int a; } SS;
}
int g(void) { int q; typedef long L; return 1 << 40; }
void h(void) { typedef int __attribute__((unused)) Q; typedef int R; R *p = 0; (void)p; typedef int G; { typedef int G; } }
