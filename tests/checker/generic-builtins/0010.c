static const char m[] = "abc";
void f(void)
{
__builtin_verbose_trap("x", m + 1);
}
