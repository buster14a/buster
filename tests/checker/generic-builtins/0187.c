int pf(void* ctx, const char* f, ...);
struct S { int a; };
void f(struct S* s)
{
__builtin_dump_struct(s, pf);
}
