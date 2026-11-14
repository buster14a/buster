#define A8 1, 2, 3, 4, 5, 6, 7, 8
#define A64 A8, A8, A8, A8, A8, A8, A8, A8
void* f(void* b)
{
return __builtin_os_log_format(b, "%d", A64, A64, A64, A64);
}
