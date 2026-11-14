int printf(const char*, ...);
struct S { int a; };
int f(struct S* s)
{
return __builtin_dump_struct(s, printf);
}
