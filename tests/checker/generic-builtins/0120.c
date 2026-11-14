struct S;
void f(struct S* p)
{
__builtin_clear_padding(p);
}
