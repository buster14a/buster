long f(void)
{
return __builtin_cpu_is(("amd")) + __builtin_cpu_supports((("sse2"))) + __builtin_allow_sanitize_check(("address")) +
__builtin_allow_runtime_check(("x")) + __builtin_annotation(3L, (const char*)"note") + __builtin_annotation(4L, ("note"));
}
