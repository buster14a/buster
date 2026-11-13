struct S { int a; };
void f(struct S s, int x)
{
__builtin_hlsl_resource_getpointer_typed();
__builtin_hlsl_resource_getpointer_typed(s, 1.0f, "x", x++, f);
(void)sizeof(__builtin_hlsl_resource_getpointer_typed(1));
}
