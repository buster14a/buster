void g(void);
void* f(void)
{
return __builtin_function_start((void*)&g + 1);
}
