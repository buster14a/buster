void* f(void* b)
{
return __builtin_os_log_format(b, "%{mask.}d", 1);
}
