int bad(const char*);
struct S { int a; };
void f(struct S* s)
{
__builtin_dump_struct(s, bad);
}
