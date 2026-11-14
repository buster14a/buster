int __attribute__((ms_abi)) f(int n, ...)
{
__builtin_ms_va_list a, b;
__builtin_ms_va_start(a, n);
__builtin_ms_va_copy(b, a);
int x = __builtin_va_arg(b, int);
__builtin_ms_va_end(b);
__builtin_ms_va_end(a);
__builtin_va_list s;
__builtin_va_copy(s, s);
return x;
}
int g(char* p, char** q, int n, ...)
{
char* c[1];
__builtin_ms_va_copy(c[0], *q);
__builtin_ms_va_end(p);
char* z;
__builtin_zos_va_start(&z, n);
__builtin_zos_va_copy(q, &z);
__builtin_zos_va_end(q);
return __builtin_va_arg(p, int) + __builtin_va_arg(*q, int);
}
