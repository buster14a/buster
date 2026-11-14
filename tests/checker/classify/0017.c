struct S { double d; };
int f(struct S s)
{
return __builtin_issubnormal(s);
}
