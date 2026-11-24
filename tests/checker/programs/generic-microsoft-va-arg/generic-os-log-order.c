static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }
static unsigned char buffer[512]; static int order[8]; static int made;
static int next(int v) { order[made++ & 7] = v; return v; }
static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }
typedef float V4 __attribute__((vector_size(16))); typedef short V2 __attribute__((vector_size(4)));
int tested(int n) { h = (unsigned)n;
__builtin_os_log_format(buffer, "%3$d %1$d %2$d", next(n), next(n + 1), next(n + 2));
bytes(__builtin_os_log_format_buffer_size("%3$d %1$d %2$d", 1, 2, 3)); for (int i = 0; i < made; i++) mix(order[i]); made = 0;
__builtin_os_log_format(buffer, "%d", n, next(5)); bytes(__builtin_os_log_format_buffer_size("%d", 1, 2)); mix(made);
__builtin_os_log_format(buffer, "%1$d %1$d %2$d", n * 3, next(n)); bytes(__builtin_os_log_format_buffer_size("%1$d %1$d %2$d", 1, 2));
__builtin_os_log_format(buffer, "%2$*1$d %2$.*1$d", n, n + 7); bytes(__builtin_os_log_format_buffer_size("%2$*1$d %2$.*1$d", 1, 1));
long double l = -1.5L * n; __int128 w = -(__int128)n << 70; V4 v = { 1.0f, 2.0f, -1.0f, (float)n }; V2 s = { (short)-n, 7 };
__builtin_os_log_format(buffer, "%Lf %d %d %d", l, w, v, s); bytes(__builtin_os_log_format_buffer_size("%Lf %d %d %d", l, w, v, s));
_Float16 f = (_Float16)(n * 0.5); __bf16 b = (__bf16)(n * 0.25f); __fp16 p = (__fp16)n;
__builtin_os_log_format(buffer, "%f %f %f", f, b, p); bytes(__builtin_os_log_format_buffer_size("%f %f %f", f, b, p));
_BitInt(24) a = -n; _BitInt(7) c = -n; _BitInt(72) d = -n; _BitInt(40) e = -n; unsigned _BitInt(24) u = (unsigned _BitInt(24))-n;
__builtin_os_log_format(buffer, "%d %d %d %d %d", a, c, d, e, u); bytes(__builtin_os_log_format_buffer_size("%d %d %d %d %d", a, c, d, e, u));
return (int)(h ^ (h >> 32)); }
