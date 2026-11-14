void* f(void* p)
{
return __builtin_ptrauth_strip(p, 0);
}
