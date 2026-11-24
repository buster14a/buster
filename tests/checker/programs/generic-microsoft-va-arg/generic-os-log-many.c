static unsigned long long h; static void mix(unsigned long long v) { h = h * 31 + v; }
static unsigned char buffer[2048]; static int made;
static int late(int v) { made++; return v * 5 + made; }
static void bytes(unsigned long n) { for (unsigned long i = 0; i < n; i++) mix(buffer[i]); }
#define F8 "%d %d %d %d %d %d %d %d "
#define A8(x) x, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7
#define F32 F8 F8 F8 F8
#define A32(x) A8(x), A8(x + 8), A8(x + 16), A8(x + 24)
#define F255 F32 F32 F32 F32 F32 F32 F32 F8 F8 F8 "%d %d %d %d %d %d %d"
int tested(int n) { h = (unsigned)n; long long q = -n; double d = 0.5 * n;
__builtin_os_log_format(buffer, F32 "%d %lld %f %{private}d %hhd %hd %d %d", A32(n), late(n), q, d, n * 3, (char)n, (short)-n, made, n);
bytes(__builtin_os_log_format_buffer_size(F32 "%d %lld %f %{private}d %hhd %hd %d %d", A32(n), late(n), q, d, n * 3, (char)n, (short)-n, made, n));
__builtin_os_log_format(buffer, "%40$d %33$d %1$d %35$f", A32(n), late(n), 0, d, 0, 0, 0, 0, n - 9);
bytes(__builtin_os_log_format_buffer_size("%40$d %33$d %1$d %35$f", A32(n), late(n), 0, d, 0, 0, 0, 0, n - 9)); mix(made);
__builtin_os_log_format(buffer, F255, A32(n), A32(n + 1), A32(n + 2), A32(n + 3), A32(n + 4), A32(n + 5), A32(n + 6),
A8(n), A8(-n), A8(n * 2), n, n + 1, n + 2, n + 3, n + 4, n + 5, late(n));
unsigned long size = __builtin_os_log_format_buffer_size(F255, A32(0), A32(1), A32(2), A32(3), A32(4), A32(5), A32(6),
A8(0), A8(1), A8(2), 0, 1, 2, 3, 4, 5, 6); mix(size); bytes(size); mix(made);
return (int)(h ^ (h >> 32)); }
