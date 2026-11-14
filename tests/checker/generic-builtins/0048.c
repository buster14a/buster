void* f(void (*q)(void))
{
return __builtin_function_start((void*)q);
}
