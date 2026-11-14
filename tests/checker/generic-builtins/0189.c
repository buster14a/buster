int pf(void* ctx, int f, ...);
struct S { int a; };
void f(struct S* s)
{
__builtin_dump_struct(s, pf, s);
}
