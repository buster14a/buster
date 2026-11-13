int f(void)
{
return __builtin_cpu_supports("not-a-feature") + __builtin_cpu_supports(("avx"));
}
