void g(void);
void* a = __builtin_function_start(0 ? g : (void (*)(void))"abc");
