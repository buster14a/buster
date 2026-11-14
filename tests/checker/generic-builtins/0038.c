void* f(void (*q)(void))
{
return __builtin_function_start(q);
}
