struct S { int a; };
_Static_assert(__builtin_os_log_format_buffer_size("%d %s", 1, "x") == 18, "");
enum { E = __builtin_os_log_format_buffer_size("%{mask.ab}d %m", 1) };
_Static_assert(E == 20, "");
_Static_assert(__builtin_os_log_format_buffer_size("%Lf %P", 1.0L, 1) == 26, "");
_Static_assert(__builtin_os_log_format_buffer_size("%2$s %1$d", 1, "x") == 18, "");
_Static_assert(__builtin_os_log_format_buffer_size("%.*s %{public, mask.abcdefghij}d", 3, "x", 1, 2) == 34, "");
_Static_assert(__builtin_os_log_format_buffer_size(u8"%d", (struct S){ 1 }) == 8, "");
_Static_assert(__builtin_os_log_format_buffer_size("%f %f %f", (_Float16)1, (__bf16)1, (__fp16)1) == 20, "");
_Static_assert(__builtin_os_log_format_buffer_size("%Lf %d %d", 1.0L, (__int128)1, (_BitInt(72))1) == 56, "");
void* f(void* b, char* c, int i, struct S s)
{
unsigned long n = __builtin_os_log_format_buffer_size("%d", i++);
(void)__builtin_os_log_format(c, ("%d"), i);
(void)__builtin_os_log_format(b, (const char*)"%s %f", "x", 1.5f);
(void)__builtin_os_log_format(b, "%d", 1, s);
(void)__builtin_os_log_format(b, "%P %{mask.ab}d", n, 1);
return __builtin_os_log_format(b, "%{private}s %%", "x");
}
_Static_assert(__builtin_os_log_format_buffer_size("%%%%%% %d\0%d%s", 1) == 8, "");
_Static_assert(__builtin_os_log_format_buffer_size("\0%d%d%d%d") == 2 && __builtin_os_log_format_buffer_size("%% %5% %{public}% %%%%") == 2, "");
_Static_assert(__builtin_os_log_format_buffer_size("%1$*2$P", (void*)0) == 12, "");
