// #2219: the complex _BitInt types of imaginary bit-precise constants, which no program can spell, as clang's
// diagnostics print them.
struct T
{
    int a;
};
int* p = 5wbi;
int* q = 0x1'0000'0000'0000'0000'0000'0000'0000'0000wbi;
void f(int n)
{
    struct T s = 5uwbi;
    int r = n << 3wbi;
    int* w = &5wbi;
    (void)s;
    (void)r;
    (void)w;
}
