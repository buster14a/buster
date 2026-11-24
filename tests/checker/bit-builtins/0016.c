struct B { unsigned x : 31; };
int f(unsigned u, struct B b)
{
return __builtin_clzg(u, b.x);
}
