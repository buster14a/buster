void __attribute__((ms_abi)) f(int n)
{
char* a;
__builtin_ms_va_start(a, n);
}
