long f(void* p)
{
return __builtin_ptrauth_sign_generic_data(p, 0);
}
