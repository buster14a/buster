int printf(const char*, ...);
struct S { struct { __int128 w : 70; } in; };
void f(struct S* s)
{
__builtin_dump_struct(s, printf);
}
