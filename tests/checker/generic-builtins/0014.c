struct S { char a[4]; };
static const struct S s = { "ab$" };
void f(void)
{
__builtin_verbose_trap("x", s.a);
}
