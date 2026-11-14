void* f(void* b, _Complex double c)
{
return __builtin_os_log_format(b, "%*d", c, 1);
}
