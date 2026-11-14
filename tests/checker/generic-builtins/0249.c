void* f(void* p)
{
return __builtin_ptrauth_sign_unauthenticated(p, 0, 0);
}
