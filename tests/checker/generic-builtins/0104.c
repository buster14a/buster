_Atomic double a;
double f(void)
{
return __arithmetic_fence(a);
}
