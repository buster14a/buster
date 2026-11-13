struct S { int a; };
void* f(void* b, struct S s)
{
return __builtin_os_log_format(b, "%d", s);
}
