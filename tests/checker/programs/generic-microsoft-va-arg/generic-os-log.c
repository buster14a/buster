static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }
static unsigned char buffer[512]; static char arr[4] = { 1, 2, 3, 4 };
static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); } int tested(int n) { h = (unsigned)n;
const char* s = (const char*)(unsigned long)(0x1000 + n);
char c = (char)(n + 3); short w = (short)(n * 7); long long q = -n; float f = 1.5f * n; double d = 0.25 * n;
void* r = __builtin_os_log_format(buffer, "%d %s %{public}s", n, s, s);
mix(r == (void*)buffer); bytes(__builtin_os_log_format_buffer_size("%d %s %{public}s", n, s, s));
__builtin_os_log_format(buffer, "%{private}c %hd %lld %f %{sensitive, public}lu", c, w, q, f, (unsigned long)n);
bytes(__builtin_os_log_format_buffer_size("%{private}c %hd %lld %f %{sensitive, public}lu", c, w, q, f, (unsigned long)n));
__builtin_os_log_format(buffer, "%m %% %.*s %.8P %{mask.abc}d %*.*d", 3, s, s, n, 4, 2, n);
bytes(__builtin_os_log_format_buffer_size("%m %% %.*s %.8P %{mask.abc}d %*.*d", 3, s, s, n, 4, 2, n));
__builtin_os_log_format(buffer, "%{public,mask.xy}.16s %e %p %s", s, d, (void*)s, arr);
unsigned long size = __builtin_os_log_format_buffer_size("%{public,mask.xy}.16s %e %p %s", s, d, (void*)s, arr);
unsigned long v = 0; for (int i = 0; i < 8; i++) v |= (unsigned long)buffer[size - 8 + i] << (8 * i);
mix(v == (unsigned long)arr); bytes(size - 8);
mix(__builtin_os_log_format_buffer_size("plain") + __builtin_os_log_format_buffer_size("%2$d %1$s", s, n) * 100 +
__builtin_os_log_format_buffer_size("%P %d", n, n) * 10000 + __builtin_os_log_format_buffer_size("%d %d", n) * 1000000);
__builtin_os_log_format(buffer, "%%%% %{public}d %%\0%s %d", n); bytes(__builtin_os_log_format_buffer_size("%%%% %{public}d %%\0%s %d", n));
return (int)(h ^ (h >> 32)); }
