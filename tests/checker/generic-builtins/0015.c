static const char m[] = "abc";
static const char* const p = m + 1;
void f(void)
{
__builtin_verbose_trap("x", p);
}
