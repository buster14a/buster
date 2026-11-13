struct S { int x; };
void f(struct S* a, struct S* b)
{
__builtin_trivially_relocate(a, b, 1);
}
