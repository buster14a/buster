void g(void);
void* f(void)
{
return __builtin_ptrauth_sign_constant(&g, 0, 0);
}
