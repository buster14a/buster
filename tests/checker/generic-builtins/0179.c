#define A8 1, 2, 3, 4, 5, 6, 7, 8
#define A64 A8, A8, A8, A8, A8, A8, A8, A8
unsigned long f(void)
{
return __builtin_os_log_format_buffer_size("%d", A64, A64, A64, A64);
}
