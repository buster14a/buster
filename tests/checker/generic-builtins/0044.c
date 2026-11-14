void g(void);
void k(void);
void* a = __builtin_function_start(1 ? g : g);
void* b = __builtin_function_start(g + 0);
void* c = __builtin_function_start(0 ? g : k);
void* d = __builtin_function_start((g, k));
void* e = __builtin_function_start(&g + 1 - 1);
void* f(void)
{
return __builtin_function_start(1 ? (char*)g + 0 : 0);
}
