void __attribute__((ms_abi)) f(int n, ...)
{
__builtin_va_list a;
__builtin_va_start(a, n);
}
