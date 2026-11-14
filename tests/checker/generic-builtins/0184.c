int printf(const char*, ...);
struct I;
void f(struct I* p)
{
__builtin_dump_struct(p, printf);
}
