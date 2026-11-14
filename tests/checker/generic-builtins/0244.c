void* f(void* p)
{
return __builtin_ptrauth_auth_load_relative_and_sign(p, 0, 0, 0, 0, 0);
}
