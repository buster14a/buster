#define A8 1, 2, 3, 4, 5, 6, 7, 8
#define A64 A8, A8, A8, A8, A8, A8, A8, A8
#define A255 A64, A64, A64, A8, A8, A8, A8, A8, A8, A8, 1, 2, 3, 4, 5, 6, 7
#define F8 "%d %d %d %d %d %d %d %d "
#define F64 F8 F8 F8 F8 F8 F8 F8 F8
_Static_assert(__builtin_os_log_format_buffer_size(F64 F64 F64 F8 F8 F8 F8 F8 F8 F8 "%d %d %d %d %d %d %d", A255) == 1532, "");
_Static_assert(__builtin_os_log_format_buffer_size("%255$lld %40$d", A255) == 14, "");
void* f(void* b, int n)
{
return __builtin_os_log_format(b, F8 F8 F8 F8 "%d %d %s", A8, A8, A8, A8, n, n + 1, "x");
}
void* g(void* b)
{
return __builtin_os_log_format(b, "%d", A255);
}
