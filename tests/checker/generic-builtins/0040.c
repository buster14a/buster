void g(void);
void h(void);
void* f(int y)
{
return __builtin_function_start(y ? g : h);
}
