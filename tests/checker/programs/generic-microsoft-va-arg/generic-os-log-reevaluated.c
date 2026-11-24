static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }
static unsigned char buffer[512]; static int made;
static int next(int v) { made++; return v + made; }
static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }
typedef char V2 __attribute__((vector_size(2)));
int tested(int n) { h = (unsigned)n; int g = n;
__builtin_os_log_format(buffer, "%1$d %2$d %1$d", g, next(n)); bytes(__builtin_os_log_format_buffer_size("%1$d %2$d %1$d", 1, 2));
__builtin_os_log_format(buffer, "%1$d %2$d %1$d", made, next(n)); bytes(__builtin_os_log_format_buffer_size("%1$d %2$d %1$d", 1, 2));
__builtin_os_log_format(buffer, "%1$d %1$d %1$d", next(n)); bytes(__builtin_os_log_format_buffer_size("%1$d %1$d %1$d", 1)); mix(made);
__float128 q = (__float128)n / 3; V2 v = { (char)n, (char)-n };
__builtin_os_log_format(buffer, "%f %d %2$d %1$f", q, v); bytes(__builtin_os_log_format_buffer_size("%f %d %2$d %1$f", q, v));
return (int)(h ^ (h >> 32)); }
