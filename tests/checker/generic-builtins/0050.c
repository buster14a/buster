struct S { int b : 3; } s;
int* f(void)
{
return __builtin_addressof(s.b);
}
