void f(void)
{
static unsigned long t = __builtin_infer_alloc_token(3);
}
