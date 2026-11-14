void* f(void* b)
{
return __builtin_os_log_format(b, "%P %{mask.abcdefghi}d", 4, 1);
}
