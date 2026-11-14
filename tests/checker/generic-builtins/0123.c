void f(int (*p)(void))
{
__builtin_clear_padding(p);
}
