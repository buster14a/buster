unsigned long strlen(const char*);
unsigned long g(void* p)
{
return __builtin_call_with_static_chain(strlen("x"), p);
}
