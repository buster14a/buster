void* f(void* p)
{
return __builtin_ptrauth_auth(p, 0, 0);
}
