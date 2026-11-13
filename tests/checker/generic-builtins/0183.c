int printf(const char*, ...);
void f(int* p)
{
__builtin_dump_struct(p, printf);
}
