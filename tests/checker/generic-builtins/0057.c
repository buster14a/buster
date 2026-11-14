typedef void F(void);
F* f(F* p)
{
return __builtin_launder(p);
}
