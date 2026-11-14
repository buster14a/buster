struct S { int x; };
int f(struct S s)
{
return __builtin_is_aligned(s, 4);
}
