void f(void)
{
const char m[] = "abc";
__builtin_verbose_trap("x", m);
}
