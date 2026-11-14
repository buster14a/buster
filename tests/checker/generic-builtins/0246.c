long f(void* p)
{
return __builtin_ptrauth_blend_discriminator(p, 0);
}
