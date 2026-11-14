int printf(const char*, ...);
struct S { _BitInt(12) b : 9; };
void f(struct S* s)
{
__builtin_dump_struct(s, printf);
}
