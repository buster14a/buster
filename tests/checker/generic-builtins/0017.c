static const char m[] = "abc";
void f(void)
{
__builtin_verbose_trap("x", (const char*)(char*)m + 1);
}
