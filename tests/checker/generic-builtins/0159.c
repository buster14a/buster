int f(int);
int g(void* p)
{
return __builtin_call_with_static_chain(f, p);
}
