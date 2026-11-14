int v;
void g(void);
void* a = __builtin_function_start(1 ? (void (*)(void))&v : g);
