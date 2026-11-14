int pf(int* ctx, const char* f, ...);
struct S { int a; };
void f(struct S* s, long* l)
{
__builtin_dump_struct(s, pf, l);
}
