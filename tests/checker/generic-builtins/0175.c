struct T { char c[300]; };
unsigned long f(struct T t)
{
return __builtin_os_log_format_buffer_size("%d", t);
}
