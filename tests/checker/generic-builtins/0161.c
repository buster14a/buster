int g(void* p)
{
return __builtin_call_with_static_chain(__builtin_abs(1), p);
}
