struct I;
struct I* f(struct I* p)
{
return __builtin_launder(p);
}
