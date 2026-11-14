void g(void);
void* a = __builtin_function_start(&g);
void* b = __builtin_function_start((&g));
void* c = __builtin_function_start((void (*)(int))g);
void* d = __builtin_function_start((void*)&g);
void* f(void)
{
return __builtin_function_start(*&g) == __builtin_function_start((char*)&g) ? a : b;
}
